#!/usr/bin/env python3
"""Add regularly spaced Custom Cues to a Wwise Music Segment through WAAPI."""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from typing import Any, Iterable, Sequence


WAAPI_OBJECT_GET = "ak.wwise.core.object.get"
WAAPI_OBJECT_SET = "ak.wwise.core.object.set"
WAAPI_OBJECT_SET_PROPERTY = "ak.wwise.core.object.setProperty"
WAAPI_OBJECT_DELETE = "ak.wwise.core.object.delete"
WAAPI_PROJECT_SAVE = "ak.wwise.core.project.save"
WAAPI_UI_GET_SELECTED_OBJECTS = "ak.wwise.ui.getSelectedObjects"
WAAPI_UNDO_BEGIN = "ak.wwise.core.undo.beginGroup"
WAAPI_UNDO_CANCEL = "ak.wwise.core.undo.cancelGroup"
WAAPI_UNDO_END = "ak.wwise.core.undo.endGroup"

CUSTOM_CUE_TYPE = 2
DEFAULT_WAAPI_URL = "ws://127.0.0.1:8080/waapi"
LOOP_CUE_NAME = "Loop"
LOOP_CUE_LEAD_MS = Decimal("10")
LOOP_CUE_OFFSET_MS = Decimal("100")
GUID_PATTERN = re.compile(
    r"^\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
    r"[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}?$"
)


class ToolError(RuntimeError):
    """An expected validation or WAAPI operation error."""


@dataclass(frozen=True)
class CueSpec:
    name: str
    time_ms: float
    index: int


@dataclass(frozen=True)
class SegmentInfo:
    object_id: str
    name: str
    path: str
    tempo: float
    end_ms: float


@dataclass(frozen=True)
class GenerationPlan:
    segment: SegmentInfo
    tempo: Decimal
    end_ms: Decimal
    specs: tuple[CueSpec, ...]
    matching_cues: tuple[dict[str, Any], ...]
    conflicting_cues: tuple[dict[str, Any], ...]
    loop_spec: CueSpec | None = None
    existing_loop_cues: tuple[dict[str, Any], ...] = ()
    target_end_ms: float | None = None
    exit_cues: tuple[dict[str, Any], ...] = ()


def positive_decimal(text: str) -> Decimal:
    try:
        value = Decimal(text)
    except InvalidOperation as exc:
        raise argparse.ArgumentTypeError(f"not a number: {text}") from exc
    if not value.is_finite() or value <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return value


def positive_beat_interval(text: str) -> Decimal:
    try:
        value = Fraction(text.strip())
    except (ValueError, ZeroDivisionError) as exc:
        raise argparse.ArgumentTypeError(
            f"not a valid beat fraction: {text} (examples: 1/4, 1/2, 1, 2)"
        ) from exc
    if value <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return Decimal(value.numerator) / Decimal(value.denominator)


def nonnegative_decimal(text: str) -> Decimal:
    try:
        value = Decimal(text)
    except InvalidOperation as exc:
        raise argparse.ArgumentTypeError(f"not a number: {text}") from exc
    if not value.is_finite() or value < 0:
        raise argparse.ArgumentTypeError("value must be zero or greater")
    return value


def nonnegative_int(text: str) -> int:
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"not an integer: {text}") from exc
    if value < 0:
        raise argparse.ArgumentTypeError("value must be zero or greater")
    return value


def positive_int(text: str) -> int:
    value = nonnegative_int(text)
    if value == 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return value


def make_cue_specs(
    *,
    tempo: Decimal,
    end_ms: Decimal,
    interval_beats: Decimal,
    start_beat: Decimal,
    prefix: str,
    start_index: int,
    padding: int,
    include_end: bool,
    max_cues: int,
) -> list[CueSpec]:
    """Build cue names and times without accumulating floating-point drift."""
    if tempo <= 0:
        raise ToolError("Tempo must be greater than zero.")
    if end_ms < 0:
        raise ToolError("Music Segment end position cannot be negative.")
    if interval_beats <= 0:
        raise ToolError("Interval must be greater than zero beats.")
    if start_beat < 0:
        raise ToolError("Start beat cannot be negative.")
    if not prefix:
        raise ToolError("Cue prefix cannot be empty.")

    beat_ms = Decimal(60000) / tempo
    current_ms = beat_ms * start_beat
    compare = (lambda value: value <= end_ms) if include_end else (lambda value: value < end_ms)

    specs: list[CueSpec] = []
    while compare(current_ms):
        if len(specs) >= max_cues:
            raise ToolError(
                f"The request would create more than {max_cues} cues. "
                "Increase --max-cues only after checking the interval and segment length."
            )
        index = start_index + len(specs)
        index_text = str(index).zfill(padding) if padding else str(index)
        specs.append(CueSpec(f"{prefix}{index_text}", float(current_ms), index))
        current_ms = beat_ms * (start_beat + interval_beats * Decimal(len(specs)))

    return specs


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Add indexed Custom Cues at a regular beat interval to a Wwise Music Segment. "
            "Wwise must be running with WAAPI enabled."
        )
    )
    parser.add_argument(
        "--segment",
        required=True,
        help="Music Segment GUID or full Wwise path.",
    )
    parser.add_argument(
        "--interval-beats",
        type=positive_beat_interval,
        default=Decimal("0.5"),
        help="Spacing in beats as a fraction (default: 1/2).",
    )
    parser.add_argument(
        "--start-beat",
        type=nonnegative_decimal,
        default=Decimal("0"),
        help="Beat offset of the first cue (default: 0).",
    )
    parser.add_argument(
        "--tempo",
        type=positive_decimal,
        help="Override the Music Segment tempo in BPM.",
    )
    parser.add_argument(
        "--end-ms",
        type=nonnegative_decimal,
        help="Override the Music Segment end position in milliseconds.",
    )
    parser.add_argument("--prefix", default="CC_", help="Cue name prefix (default: CC_).")
    parser.add_argument(
        "--start-index",
        type=nonnegative_int,
        default=0,
        help="First numeric suffix (default: 0).",
    )
    parser.add_argument(
        "--padding",
        type=nonnegative_int,
        default=0,
        help="Minimum index width; use 0 for no zero padding (default: 0).",
    )
    parser.add_argument(
        "--include-end",
        action="store_true",
        help="Create a Custom Cue at the Exit Cue position when it lands exactly on the interval.",
    )
    parser.add_argument(
        "--loop",
        action="store_true",
        help="Ensure a User cue named 'Loop' exists 100 ms before the Music Segment End Cursor.",
    )
    parser.add_argument(
        "--replace-existing",
        action="store_true",
        help="Delete existing User cues whose names start with --prefix before creating cues.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Connect and print the planned cues without changing Wwise.",
    )
    parser.add_argument(
        "--save",
        action="store_true",
        help="Save the Wwise project after creating cues. By default the project remains dirty and undoable.",
    )
    parser.add_argument(
        "--batch-size",
        type=positive_int,
        default=250,
        help="Number of cues per WAAPI object.set call (default: 250).",
    )
    parser.add_argument(
        "--max-cues",
        type=positive_int,
        default=10000,
        help="Safety limit for generated cues (default: 10000).",
    )
    parser.add_argument(
        "--waapi-url",
        default=DEFAULT_WAAPI_URL,
        help=f"WAAPI WebSocket URL (default: {DEFAULT_WAAPI_URL}).",
    )
    return parser


def _query_from_reference(reference: str) -> dict[str, list[str]]:
    if GUID_PATTERN.fullmatch(reference):
        guid = reference if reference.startswith("{") else f"{{{reference}}}"
        return {"id": [guid]}
    if reference.startswith("\\"):
        return {"path": [reference]}
    raise ToolError("--segment must be a GUID or a full Wwise path beginning with '\\'.")


def _single_result(result: Any, description: str) -> dict[str, Any]:
    values = result.get("return", []) if isinstance(result, dict) else []
    if len(values) != 1:
        raise ToolError(f"Expected one {description}, but WAAPI returned {len(values)}.")
    return values[0]


def resolve_segment(client: Any, reference: str) -> SegmentInfo:
    result = client.call(
        WAAPI_OBJECT_GET,
        {"from": _query_from_reference(reference)},
        options={"return": ["id", "name", "type", "path", "@Tempo", "@EndPosition"]},
    )
    obj = _single_result(result, "Music Segment")
    if obj.get("type") != "MusicSegment":
        raise ToolError(
            f"Target is type '{obj.get('type', 'unknown')}', not 'MusicSegment': "
            f"{obj.get('path', reference)}"
        )

    try:
        tempo = float(obj["@Tempo"])
        end_ms = float(obj["@EndPosition"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ToolError("WAAPI did not return valid @Tempo and @EndPosition values.") from exc
    if not math.isfinite(tempo) or tempo <= 0:
        raise ToolError(f"Music Segment has an invalid tempo: {tempo}")
    if not math.isfinite(end_ms) or end_ms < 0:
        raise ToolError(f"Music Segment has an invalid end position: {end_ms}")

    return SegmentInfo(
        object_id=obj["id"],
        name=obj.get("name", ""),
        path=obj.get("path", reference),
        tempo=tempo,
        end_ms=end_ms,
    )


def _segment_from_object(obj: dict[str, Any]) -> SegmentInfo:
    try:
        tempo = float(obj["@Tempo"])
        end_ms = float(obj["@EndPosition"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ToolError(
            f"Music Segment '{obj.get('name', '')}' has invalid tempo or end-position data."
        ) from exc
    if not math.isfinite(tempo) or tempo <= 0 or not math.isfinite(end_ms) or end_ms < 0:
        raise ToolError(
            f"Music Segment '{obj.get('name', '')}' has invalid tempo or end-position data."
        )
    return SegmentInfo(
        object_id=obj["id"],
        name=obj.get("name", ""),
        path=obj.get("path", obj.get("name", "")),
        tempo=tempo,
        end_ms=end_ms,
    )


def list_music_segments(client: Any) -> list[SegmentInfo]:
    result = client.call(
        WAAPI_OBJECT_GET,
        {"waql": "from type MusicSegment"},
        options={"return": ["id", "name", "type", "path", "@Tempo", "@EndPosition"]},
    )
    values = result.get("return", []) if isinstance(result, dict) else []
    segments = [_segment_from_object(obj) for obj in values if obj.get("type") == "MusicSegment"]
    return sorted(segments, key=lambda segment: segment.path.casefold())


def get_selected_music_segments(client: Any) -> list[SegmentInfo]:
    result = client.call(
        WAAPI_UI_GET_SELECTED_OBJECTS,
        {},
        options={"return": ["id", "name", "type", "path", "@Tempo", "@EndPosition"]},
    )
    values = result.get("objects", []) if isinstance(result, dict) else []
    segments: list[SegmentInfo] = []
    for obj in values:
        if obj.get("type") == "MusicSegment":
            segments.append(_segment_from_object(obj))
            continue
        object_id = obj.get("id")
        if not object_id:
            continue
        ancestors_result = client.call(
            WAAPI_OBJECT_GET,
            {"waql": f'"{object_id}" select ancestors where type = "MusicSegment"'},
            options={"return": ["id", "name", "type", "path", "@Tempo", "@EndPosition"]},
        )
        ancestors = ancestors_result.get("return", []) if isinstance(ancestors_result, dict) else []
        segments.extend(
            _segment_from_object(ancestor)
            for ancestor in ancestors
            if ancestor.get("type") == "MusicSegment"
        )

    unique: dict[str, SegmentInfo] = {}
    for segment in segments:
        unique.setdefault(segment.object_id, segment)
    return list(unique.values())


def get_custom_cues(client: Any, segment_id: str) -> list[dict[str, Any]]:
    return [
        cue
        for cue in get_music_cues(client, segment_id)
        if cue.get("@CueType") == CUSTOM_CUE_TYPE
    ]


def get_music_cues(client: Any, segment_id: str) -> list[dict[str, Any]]:
    # Cues are object-list items, so descendants does not include them. Query by owner instead.
    result = client.call(
        WAAPI_OBJECT_GET,
        {"waql": f'from type MusicCue where owner.id = "{segment_id}"'},
        options={"return": ["id", "name", "type", "owner.id", "@CueType", "@TimeMs"]},
    )
    values = result.get("return", []) if isinstance(result, dict) else []
    return [cue for cue in values if cue.get("owner.id") == segment_id]


def get_segment_content_end_ms(client: Any, segment: SegmentInfo) -> Decimal | None:
    result = client.call(
        WAAPI_OBJECT_GET,
        {"from": {"ofType": ["MusicClip", "MusicClipMidi"]}},
        options={
            "return": [
                "id",
                "name",
                "type",
                "path",
                "@PlayAt",
                "@BeginTrimOffset",
                "@EndTrimOffset",
            ]
        },
    )
    values = result.get("return", []) if isinstance(result, dict) else []
    path_prefix = segment.path.rstrip("\\") + "\\"
    content_ends: list[Decimal] = []
    for clip in values:
        if not str(clip.get("path", "")).startswith(path_prefix):
            continue
        try:
            play_at = Decimal(str(clip["@PlayAt"]))
            begin_trim = Decimal(str(clip["@BeginTrimOffset"]))
            end_trim = Decimal(str(clip["@EndTrimOffset"]))
        except (KeyError, InvalidOperation) as exc:
            raise ToolError(f"Music Clip has invalid position data: {clip.get('path', '')}") from exc
        duration = end_trim - begin_trim
        if play_at < 0 or duration < 0:
            raise ToolError(f"Music Clip has invalid position data: {clip.get('path', '')}")
        content_ends.append(play_at + duration)
    return max(content_ends) if content_ends else None


def calculate_loop_positions(content_end_ms: Decimal) -> tuple[Decimal, Decimal]:
    if content_end_ms < LOOP_CUE_LEAD_MS:
        raise ToolError(
            f"Music Segment content end must be at least {LOOP_CUE_LEAD_MS} ms "
            "to create the Loop cue."
        )
    loop_time_ms = content_end_ms - LOOP_CUE_LEAD_MS
    return loop_time_ms, loop_time_ms + LOOP_CUE_OFFSET_MS


def chunks(values: Sequence[CueSpec], size: int) -> Iterable[Sequence[CueSpec]]:
    for start in range(0, len(values), size):
        yield values[start : start + size]


def create_cues(client: Any, segment_id: str, specs: Sequence[CueSpec], batch_size: int) -> None:
    for batch in chunks(specs, batch_size):
        cues = [
            {
                "type": "MusicCue",
                "name": spec.name,
                "@TimeMs": spec.time_ms,
                "@CueType": CUSTOM_CUE_TYPE,
            }
            for spec in batch
        ]
        client.call(
            WAAPI_OBJECT_SET,
            {
                "objects": [{"object": segment_id, "@Cues": cues}],
                "onNameConflict": "fail",
                "listMode": "append",
            },
        )


def delete_cues(client: Any, cues: Sequence[dict[str, Any]]) -> None:
    for cue in cues:
        client.call(WAAPI_OBJECT_DELETE, {"object": cue["id"]})


def prepare_generation(
    client: Any,
    *,
    segment_reference: str,
    interval_beats: Decimal,
    start_beat: Decimal,
    prefix: str,
    start_index: int,
    padding: int,
    include_end: bool,
    max_cues: int,
    tempo_override: Decimal | None = None,
    end_ms_override: Decimal | None = None,
    add_loop: bool = False,
) -> GenerationPlan:
    segment = resolve_segment(client, segment_reference)
    tempo = tempo_override or Decimal(str(segment.tempo))
    all_cues = get_music_cues(client, segment.object_id)
    custom_cues = [cue for cue in all_cues if cue.get("@CueType") == CUSTOM_CUE_TYPE]
    existing_loop_cues = [cue for cue in custom_cues if cue.get("name") == LOOP_CUE_NAME]
    exit_cues = [cue for cue in all_cues if cue.get("@CueType") == 1]

    loop_spec = None
    target_end_ms = None
    if add_loop:
        if len(exit_cues) != 1:
            raise ToolError(
                f"Expected exactly one Exit Cue, but Wwise returned {len(exit_cues)}."
            )
        content_end_ms = get_segment_content_end_ms(client, segment)
        if content_end_ms is None:
            if existing_loop_cues:
                content_end_ms = (
                    Decimal(str(existing_loop_cues[0].get("@TimeMs", 0)))
                    + LOOP_CUE_LEAD_MS
                )
            else:
                content_end_ms = Decimal(str(segment.end_ms))
        loop_time_ms, end_cursor_ms = calculate_loop_positions(content_end_ms)
        loop_spec = CueSpec(LOOP_CUE_NAME, float(loop_time_ms), -1)
        target_end_ms = float(end_cursor_ms)

    end_ms = (
        end_ms_override
        if end_ms_override is not None
        else Decimal(str(loop_spec.time_ms))
        if loop_spec is not None
        else Decimal(str(segment.end_ms))
    )
    specs = make_cue_specs(
        tempo=tempo,
        end_ms=end_ms,
        interval_beats=interval_beats,
        start_beat=start_beat,
        prefix=prefix,
        start_index=start_index,
        padding=padding,
        include_end=include_end,
        max_cues=max_cues,
    )
    event_cues = [cue for cue in custom_cues if cue.get("name") != LOOP_CUE_NAME]
    matching = [cue for cue in event_cues if cue.get("name", "").startswith(prefix)]
    planned_names = {spec.name for spec in specs}
    conflicts = [cue for cue in event_cues if cue.get("name") in planned_names]
    return GenerationPlan(
        segment=segment,
        tempo=tempo,
        end_ms=end_ms,
        specs=tuple(specs),
        matching_cues=tuple(matching),
        conflicting_cues=tuple(conflicts),
        loop_spec=loop_spec,
        existing_loop_cues=tuple(existing_loop_cues),
        target_end_ms=target_end_ms,
        exit_cues=tuple(exit_cues),
    )


def execute_generation(
    client: Any,
    plan: GenerationPlan,
    *,
    replace_existing: bool,
    batch_size: int,
    save: bool,
) -> int:
    if plan.conflicting_cues and not replace_existing:
        names = ", ".join(str(cue.get("name")) for cue in plan.conflicting_cues[:5])
        suffix = "..." if len(plan.conflicting_cues) > 5 else ""
        raise ToolError(
            f"{len(plan.conflicting_cues)} cue name(s) already exist ({names}{suffix}). "
            "Enable replacement or change the prefix/start index."
        )
    if not plan.specs and not (replace_existing and plan.matching_cues):
        if plan.loop_spec is None:
            return 0

    client.call(WAAPI_UNDO_BEGIN, {})
    undo_open = True
    try:
        if plan.loop_spec is not None:
            if plan.target_end_ms is None:
                raise ToolError("Loop plan is missing the target End Cursor.")
            if len(plan.exit_cues) != 1 or not plan.exit_cues[0].get("id"):
                raise ToolError("Loop plan must contain exactly one valid Exit Cue.")
            client.call(
                WAAPI_OBJECT_SET_PROPERTY,
                {
                    "object": plan.segment.object_id,
                    "property": "EndPosition",
                    "value": plan.target_end_ms,
                },
            )
            client.call(
                WAAPI_OBJECT_SET_PROPERTY,
                {
                    "object": plan.exit_cues[0]["id"],
                    "property": "TimeMs",
                    "value": plan.target_end_ms,
                },
            )
        cues_to_delete = list(plan.existing_loop_cues if plan.loop_spec else ())
        if replace_existing:
            cues_to_delete.extend(plan.matching_cues)
        unique_cues = {
            cue["id"]: cue for cue in cues_to_delete if cue.get("id")
        }
        delete_cues(client, tuple(unique_cues.values()))
        specs_to_create = plan.specs + ((plan.loop_spec,) if plan.loop_spec else ())
        create_cues(client, plan.segment.object_id, specs_to_create, batch_size)
        if plan.loop_spec is not None:
            segment_result = client.call(
                WAAPI_OBJECT_GET,
                {"from": {"id": [plan.segment.object_id]}},
                options={"return": ["id", "@EndPosition"]},
            )
            updated_segment = _single_result(segment_result, "Music Segment after update")
            try:
                end_position_ms = float(updated_segment["@EndPosition"])
            except (KeyError, TypeError, ValueError) as exc:
                raise ToolError(
                    "End Cursor verification failed: Wwise returned an invalid value."
                ) from exc
            if not math.isclose(end_position_ms, plan.target_end_ms, abs_tol=0.001):
                raise ToolError(
                    f"End Cursor verification failed: expected {plan.target_end_ms} ms, "
                    f"got {end_position_ms} ms."
                )
            updated_cues = get_music_cues(client, plan.segment.object_id)
            updated_exit_cues = [cue for cue in updated_cues if cue.get("@CueType") == 1]
            if len(updated_exit_cues) != 1:
                raise ToolError(
                    f"Expected exactly one Exit Cue after update, but Wwise returned "
                    f"{len(updated_exit_cues)}."
                )
            exit_time_ms = float(updated_exit_cues[0].get("@TimeMs", -1))
            if not math.isclose(exit_time_ms, plan.target_end_ms, abs_tol=0.001):
                raise ToolError(
                    f"Exit Cue verification failed: expected {plan.target_end_ms} ms, "
                    f"got {exit_time_ms} ms."
                )
        client.call(
            WAAPI_UNDO_END,
            {"displayName": f"Generate {len(specs_to_create)} Custom Cues"},
        )
        undo_open = False
        if save:
            client.call(WAAPI_PROJECT_SAVE, {})
    except Exception:
        if undo_open:
            client.call(WAAPI_UNDO_CANCEL, {"undo": True})
        raise
    return len(plan.specs) + (1 if plan.loop_spec else 0)


def print_plan(segment: SegmentInfo, tempo: Decimal, end_ms: Decimal, specs: Sequence[CueSpec]) -> None:
    print(f"Segment : {segment.path}")
    print(f"Tempo   : {tempo} BPM")
    print(f"End     : {end_ms} ms")
    print(f"Cues    : {len(specs)}")
    if not specs:
        return
    preview = list(specs[:5])
    if len(specs) > 10:
        preview.append(None)  # type: ignore[arg-type]
        preview.extend(specs[-5:])
    elif len(specs) > 5:
        preview.extend(specs[5:])
    print("\nName                      Time (ms)")
    print("------------------------  ---------")
    for spec in preview:
        if spec is None:
            print("...")
        else:
            print(f"{spec.name:<24}  {spec.time_ms:>9.3f}")


def run(args: argparse.Namespace) -> int:
    try:
        from waapi import CannotConnectToWaapiException, WaapiClient, WaapiRequestFailed
    except ImportError as exc:
        raise ToolError(
            "The 'waapi-client' package is not installed. Run: "
            "python -m pip install -r requirements.txt"
        ) from exc

    try:
        with WaapiClient(args.waapi_url, allow_exception=True) as client:
            plan = prepare_generation(
                client,
                segment_reference=args.segment,
                interval_beats=args.interval_beats,
                start_beat=args.start_beat,
                prefix=args.prefix,
                start_index=args.start_index,
                padding=args.padding,
                include_end=args.include_end,
                max_cues=args.max_cues,
                tempo_override=args.tempo,
                end_ms_override=args.end_ms,
                add_loop=args.loop,
            )
            print_plan(plan.segment, plan.tempo, plan.end_ms, plan.specs)
            if plan.loop_spec:
                print(f"Loop    : {plan.loop_spec.time_ms:.3f} ms")
                print(f"End/Exit: {plan.target_end_ms:.3f} ms")

            if args.dry_run:
                if plan.matching_cues:
                    action = "replace" if args.replace_existing else "keep"
                    print(f"\nExisting matching cues: {len(plan.matching_cues)} ({action} on write)")
                print("Dry run: Wwise was not modified.")
                return 0
            if (
                not plan.specs
                and plan.loop_spec is None
                and not (args.replace_existing and plan.matching_cues)
            ):
                print("No cues fall within the requested range; Wwise was not modified.")
                return 0
            created_count = execute_generation(
                client,
                plan,
                replace_existing=args.replace_existing,
                batch_size=args.batch_size,
                save=args.save,
            )

            print(
                f"\nCreated {created_count} cue(s)"
                + (
                    f" and replaced {len(plan.matching_cues)} existing cue(s)"
                    if args.replace_existing
                    else ""
                )
                + "."
            )
            print("Wwise project saved." if args.save else "Review the result in Wwise, then save the project.")
            return 0
    except CannotConnectToWaapiException as exc:
        raise ToolError(
            f"Cannot connect to WAAPI at {args.waapi_url}. "
            "Start Wwise, open the project, and enable WAAPI."
        ) from exc
    except WaapiRequestFailed as exc:
        error_text = str(exc)
        if "ak.wwise.locked" in error_text:
            raise ToolError(
                "Wwise is waiting for a modal dialog. Close or confirm the dialog in Wwise, "
                "then run the command again."
            ) from exc
        if "ak.wwise.query.unknown_object" in error_text:
            raise ToolError(
                "The Music Segment could not be found in the open Wwise project. "
                "Use its GUID or copy its full object path from Wwise."
            ) from exc
        raise ToolError(f"WAAPI request failed: {exc}") from exc


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run(args)
    except ToolError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
