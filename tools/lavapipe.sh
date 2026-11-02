# Sourced by the golden harness. Keep loader selection independent of the
# caller's desktop GPU and fail before rendering if the ICD is unavailable.
# shellcheck shell=bash
pin_lavapipe() {
    local icd="$1"
    if [[ ! -f "$icd" || ! -r "$icd" ]]; then
        echo "golden: missing or unreadable lavapipe ICD: $icd; Infra Lead must reinstall mesa-vulkan-drivers" >&2
        return 2
    fi
    export VK_ICD_FILENAMES="$icd" VK_DRIVER_FILES="$icd"
}
