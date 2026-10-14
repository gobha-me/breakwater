extends RefCounted
## A campaign in progress: which one, which mission, and the logs of the
## missions already won. Holds no rules: the seeds, each mission's score and
## the campaign log all come from the core (docs/RULES.md, "Campaign").

## The campaign number, or -1 outside a campaign.
var number := -1
## 0-based index of the mission being played.
var index := 0
var seeds := PackedInt64Array()
## Logs of the missions won before this one, in order.
var won_logs := PackedStringArray()
## Their scores' sum.
var banked := 0
## True once a mission was lost or the last one won.
var over := false


func active() -> bool:
	return number >= 0


func start(k: int) -> bool:
	var s := BreakwaterMission.campaign_seeds(k)
	if s.is_empty():
		return false
	number = k
	index = 0
	seeds = s
	won_logs = PackedStringArray()
	banked = 0
	over = false
	return true


func seed() -> int:
	return seeds[index]


func length() -> int:
	return seeds.size()


## The campaign's score with `current` as the mission in play.
func score(current: BreakwaterMission) -> int:
	return banked + current.get_score()


## Called when `current` has ended. A win banks it; the last win, or any loss,
## ends the campaign. Returns whether there is a next mission to play.
func mission_ended(current: BreakwaterMission) -> bool:
	if current.get_outcome() == BreakwaterMission.OUTCOME_WON and index + 1 < seeds.size():
		return true
	over = true
	return false


## Banks the won mission `current` and moves on to the next.
func advance(current: BreakwaterMission) -> void:
	won_logs.append(current.get_log())
	banked += current.get_score()
	index += 1


## The campaign log so far, with `current` as the last mission: {error, text}.
func log_text(current: BreakwaterMission) -> Dictionary:
	var logs := won_logs.duplicate()
	logs.append(current.get_log())
	return BreakwaterMission.campaign_log(number, logs)


func status(current: BreakwaterMission) -> String:
	var line := "Campaign %d   ·   mission %d / %d   ·   score %d" % [number, index + 1, seeds.size(), score(current)]
	if over:
		var won := current.get_outcome() == BreakwaterMission.OUTCOME_WON
		line += "   ·   %s" % ("complete" if won else "over")
	return line
