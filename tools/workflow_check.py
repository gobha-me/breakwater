#!/usr/bin/env python3
"""Cheap, fail-closed policy checks for GitHub and future Gitea workflows.

Requires PyYAML 6.0.3. Preserve scalar text except for YAML null values.
"""

import argparse
import re
import sys
from pathlib import Path

import yaml


class UniqueLoader(yaml.BaseLoader):
    """Reject duplicate keys instead of silently checking the last value."""


def unique_mapping(loader, node):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node)
        if not isinstance(key, str) or key in result:
            raise ValueError(f"invalid or duplicate mapping key: {key!r}")
        result[key] = loader.construct_object(value_node)
    return result


UniqueLoader.add_constructor("tag:yaml.org,2002:map", unique_mapping)
# Keep `on` and numeric timeouts as text, but never treat an absent branch
# name as a literal string. Quoted null spellings remain valid string names.
UniqueLoader.add_implicit_resolver(
    "tag:yaml.org,2002:null",
    re.compile(r"^(?:~|null|Null|NULL|)$"),
    ["~", "n", "N", ""],
)
UniqueLoader.add_constructor(
    "tag:yaml.org,2002:null", yaml.SafeLoader.construct_yaml_null
)
CANCEL = "${{ github.event_name == 'pull_request' }}"
GITEA_CANCEL = "${{ gitea.event_name == 'push' && gitea.ref != 'refs/heads/main' }}"


def check(path, forge):
    errors = []

    def require(condition, message):
        if not condition:
            errors.append(message)

    try:
        data = yaml.load(path.read_text(), Loader=UniqueLoader)
        if not isinstance(data, dict):
            raise TypeError("workflow must be a mapping")
        concurrency = data.get("concurrency", {})
        require(isinstance(concurrency, dict), "concurrency must be a mapping")
        if isinstance(concurrency, dict):
            group = concurrency.get("group")
            require(
                isinstance(group, str) and bool(group.strip()),
                "concurrency group is required",
            )
            require(
                isinstance(group, str)
                and f"${{{{ {forge}.workflow }}}}" in group
                and f"${{{{ {forge}.ref }}}}" in group,
                "concurrency group must isolate workflow and ref",
            )
            expected = CANCEL if forge == "github" else GITEA_CANCEL
            require(
                concurrency.get("cancel-in-progress") == expected,
                f"cancel-in-progress must be {expected} to preserve main/manual runs",
            )
        events = data.get("on")
        if not isinstance(events, dict) or not events:
            raise ValueError("on must explicitly map allowed triggers")
        allowed = (
            {"push", "pull_request", "workflow_dispatch"}
            if forge == "github"
            else {"push", "schedule", "workflow_dispatch"}
        )
        require(set(events) <= allowed, "unsupported trigger")
        if forge == "github":
            require(
                "pull_request" in events, "GitHub light check must run on pull_request"
            )
        if "push" in events:
            push = events["push"]
            branches = push.get("branches") if isinstance(push, dict) else None
            valid = (
                isinstance(branches, list)
                and bool(branches)
                and all(
                    isinstance(branch, str)
                    and bool(branch.strip())
                    and not any(char in branch for char in "*?+![\\")
                    for branch in branches
                )
            )
            require(valid, "push requires named branches without globs")
            if forge == "github":
                require(
                    branches == ["main"], "GitHub push branches must be exactly [main]"
                )
            if isinstance(push, dict):
                require(
                    not ({"tags", "tags-ignore", "branches-ignore"} & set(push)),
                    "push tags and negative branch filters are forbidden",
                )
        jobs = data.get("jobs")
        if not isinstance(jobs, dict) or not jobs:
            raise ValueError("jobs must be a nonempty mapping")
        if forge == "github":
            require(len(jobs) == 1, "GitHub must keep exactly one light job")
        for name, job in jobs.items():
            if not isinstance(job, dict):
                raise TypeError(f"job {name} must be a mapping")
            timeout = job.get("timeout-minutes", "")
            require(
                isinstance(timeout, str)
                and timeout.isdecimal()
                and 0 < int(timeout) <= (10 if forge == "github" else 179),
                f"job {name}: timeout must be 1..{10 if forge == 'github' else 179} minutes",
            )
            if forge == "gitea":
                require(
                    job.get("runs-on") in ("ci-kind", "ci-build", "ci-light"),
                    f"job {name}: unsupported runner",
                )
            require(
                "concurrency" not in job,
                f"job {name}: keep concurrency at workflow scope",
            )
    except (OSError, TypeError, ValueError, yaml.YAMLError) as exc:
        errors.append(str(exc))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parent.parent
    )
    args = parser.parse_args()
    failures = []
    count = 0
    for forge in ("github", "gitea"):
        directory = args.root / f".{forge}" / "workflows"
        paths = sorted(
            path for path in directory.glob("*") if path.suffix in (".yml", ".yaml")
        )
        if forge == "github" and len(paths) != 1:
            failures.append("GitHub must contain exactly one light workflow")
        for path in paths:
            count += 1
            failures.extend(
                f"{path.relative_to(args.root)}: {error}"
                for error in check(path, forge)
            )
    for failure in failures:
        print(f"workflow: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"workflow: {count} workflow(s) satisfy policy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
