import unittest
from decimal import Decimal
from unittest.mock import Mock

from add_music_segment_custom_cues import (
    CUSTOM_CUE_TYPE,
    CueSpec,
    GenerationPlan,
    LOOP_CUE_NAME,
    SegmentInfo,
    ToolError,
    WAAPI_OBJECT_DELETE,
    WAAPI_OBJECT_GET,
    WAAPI_PROJECT_SAVE,
    WAAPI_OBJECT_SET,
    WAAPI_OBJECT_SET_PROPERTY,
    WAAPI_UI_GET_SELECTED_OBJECTS,
    WAAPI_UNDO_BEGIN,
    WAAPI_UNDO_CANCEL,
    WAAPI_UNDO_END,
    build_parser,
    create_cues,
    execute_generation,
    get_music_cues,
    get_selected_music_segments,
    list_music_segments,
    make_cue_specs,
    positive_beat_interval,
    prepare_generation,
)
from music_segment_custom_cue_gui import (
    build_preview_specs,
    filter_music_segments,
    load_event_type_config,
)


class ParserDefaultsTests(unittest.TestCase):
    def test_cli_defaults_match_gui_naming_policy(self) -> None:
        args = build_parser().parse_args(["--segment", "{SEGMENT-ID}"])

        self.assertEqual("CC_", args.prefix)
        self.assertEqual(0, args.padding)
        self.assertEqual(Decimal("0"), args.start_beat)
        self.assertFalse(args.loop)

    def test_fractional_beat_interval_is_parsed_exactly(self) -> None:
        self.assertEqual(Decimal("0.5"), positive_beat_interval("1/2"))
        self.assertEqual(Decimal("0.125"), positive_beat_interval("1/8"))


class EventTypeConfigTests(unittest.TestCase):
    def test_config_maps_event_types_to_unique_cue_names(self) -> None:
        items = load_event_type_config()

        self.assertGreater(len(items), 0)
        self.assertTrue(all(item.event_type for item in items))
        self.assertTrue(all(item.custom_cue_name for item in items))
        self.assertEqual(len(items), len({item.event_type for item in items}))
        self.assertEqual(len(items), len({item.custom_cue_name for item in items}))


class PreviewSpecsTests(unittest.TestCase):
    def test_current_end_cursor_and_existing_exit_cue_are_in_preview(self) -> None:
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(CueSpec("Cue_0", 0.0, 0),),
            matching_cues=(),
            conflicting_cues=(),
            exit_cues=(
                {"id": "{EXIT}", "name": "Exit Cue", "@CueType": 1, "@TimeMs": 950},
            ),
        )

        preview = build_preview_specs(plan)

        self.assertEqual(
            [("Cue_0", 0.0), ("Exit Cue", 950.0), ("End Cursor", 1000.0)],
            [(spec.name, spec.time_ms) for spec in preview],
        )

    def test_loop_preview_uses_target_end_and_exit_positions(self) -> None:
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(),
            matching_cues=(),
            conflicting_cues=(),
            loop_spec=CueSpec(LOOP_CUE_NAME, 1000.0, -1),
            target_end_ms=1100.0,
            exit_cues=(
                {"id": "{EXIT}", "name": "Exit Cue", "@CueType": 1, "@TimeMs": 1000},
            ),
        )

        preview = build_preview_specs(plan)

        self.assertEqual(
            [(LOOP_CUE_NAME, 1000.0), ("End Cursor", 1100.0), ("Exit Cue", 1100.0)],
            [(spec.name, spec.time_ms) for spec in preview],
        )


class SegmentFilterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.segments = [
            SegmentInfo("{A}", "Battle_Loop", "\\Music\\Combat\\Battle_Loop", 120, 1000),
            SegmentInfo("{B}", "Battle_Intro", "\\Music\\Combat\\Battle_Intro", 120, 500),
            SegmentInfo("{C}", "Town", "\\Music\\Ambient\\Town", 90, 2000),
        ]

    def test_fuzzy_filter_matches_case_insensitive_name_and_path_tokens(self) -> None:
        matches, error = filter_music_segments(
            self.segments, "COMBAT loop", use_regex=False
        )

        self.assertIsNone(error)
        self.assertEqual(["{A}"], [segment.object_id for segment in matches])

    def test_empty_filter_returns_all_segments(self) -> None:
        matches, error = filter_music_segments(self.segments, "  ", use_regex=False)

        self.assertIsNone(error)
        self.assertEqual(self.segments, matches)

    def test_regex_filter_matches_name_or_full_path(self) -> None:
        matches, error = filter_music_segments(
            self.segments, r"Battle_(Loop|Intro)$", use_regex=True
        )

        self.assertIsNone(error)
        self.assertEqual(["{A}", "{B}"], [segment.object_id for segment in matches])

    def test_invalid_regex_returns_error(self) -> None:
        matches, error = filter_music_segments(self.segments, "[", use_regex=True)

        self.assertEqual([], matches)
        self.assertIsNotNone(error)


class MakeCueSpecsTests(unittest.TestCase):
    def test_half_beat_at_150_bpm_matches_sample_segment(self) -> None:
        specs = make_cue_specs(
            tempo=Decimal("150"),
            end_ms=Decimal("12800"),
            interval_beats=Decimal("0.5"),
            start_beat=Decimal("0"),
            prefix="CustomCue_",
            start_index=0,
            padding=4,
            include_end=False,
            max_cues=1000,
        )

        self.assertEqual(64, len(specs))
        self.assertEqual("CustomCue_0000", specs[0].name)
        self.assertEqual(0.0, specs[0].time_ms)
        self.assertEqual("CustomCue_0063", specs[-1].name)
        self.assertEqual(12600.0, specs[-1].time_ms)

    def test_start_beat_index_and_padding(self) -> None:
        specs = make_cue_specs(
            tempo=Decimal("120"),
            end_ms=Decimal("2000"),
            interval_beats=Decimal("1"),
            start_beat=Decimal("0.5"),
            prefix="Beat-",
            start_index=8,
            padding=2,
            include_end=False,
            max_cues=100,
        )

        self.assertEqual([250.0, 750.0, 1250.0, 1750.0], [cue.time_ms for cue in specs])
        self.assertEqual(["Beat-08", "Beat-09", "Beat-10", "Beat-11"], [cue.name for cue in specs])

    def test_include_end_adds_exact_boundary(self) -> None:
        common = dict(
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            interval_beats=Decimal("1"),
            start_beat=Decimal("0"),
            prefix="Cue_",
            start_index=0,
            padding=0,
            max_cues=100,
        )

        without_end = make_cue_specs(include_end=False, **common)
        with_end = make_cue_specs(include_end=True, **common)

        self.assertEqual([0.0, 500.0], [cue.time_ms for cue in without_end])
        self.assertEqual([0.0, 500.0, 1000.0], [cue.time_ms for cue in with_end])

    def test_fractional_interval_does_not_accumulate_float_error(self) -> None:
        specs = make_cue_specs(
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            interval_beats=Decimal("0.1"),
            start_beat=Decimal("0"),
            prefix="Cue_",
            start_index=0,
            padding=0,
            include_end=False,
            max_cues=100,
        )

        self.assertEqual(20, len(specs))
        self.assertEqual(950.0, specs[-1].time_ms)

    def test_safety_limit_is_enforced(self) -> None:
        with self.assertRaises(ToolError):
            make_cue_specs(
                tempo=Decimal("120"),
                end_ms=Decimal("10000"),
                interval_beats=Decimal("0.01"),
                start_beat=Decimal("0"),
                prefix="Cue_",
                start_index=0,
                padding=0,
                include_end=False,
                max_cues=10,
            )


class CreateCuesTests(unittest.TestCase):
    def test_uses_music_cue_list_and_user_cue_type_in_batches(self) -> None:
        client = Mock()
        specs = [
            CueSpec("CustomCue_0000", 0.0, 0),
            CueSpec("CustomCue_0001", 200.0, 1),
            CueSpec("CustomCue_0002", 400.0, 2),
        ]

        create_cues(client, "{SEGMENT-ID}", specs, batch_size=2)

        self.assertEqual(2, client.call.call_count)
        first_uri, first_args = client.call.call_args_list[0].args
        self.assertEqual(WAAPI_OBJECT_SET, first_uri)
        self.assertEqual("append", first_args["listMode"])
        self.assertEqual("fail", first_args["onNameConflict"])
        self.assertEqual("{SEGMENT-ID}", first_args["objects"][0]["object"])
        cues = first_args["objects"][0]["@Cues"]
        self.assertEqual(["CustomCue_0000", "CustomCue_0001"], [cue["name"] for cue in cues])
        self.assertEqual([0.0, 200.0], [cue["@TimeMs"] for cue in cues])
        self.assertTrue(all(cue["type"] == "MusicCue" for cue in cues))
        self.assertTrue(all(cue["@CueType"] == CUSTOM_CUE_TYPE for cue in cues))


class WaapiIntegrationStructureTests(unittest.TestCase):
    def test_lists_and_sorts_music_segments(self) -> None:
        client = Mock()
        client.call.return_value = {
            "return": [
                {
                    "id": "{B}",
                    "name": "B",
                    "type": "MusicSegment",
                    "path": "\\Music\\B",
                    "@Tempo": 100,
                    "@EndPosition": 2000,
                },
                {
                    "id": "{A}",
                    "name": "A",
                    "type": "MusicSegment",
                    "path": "\\Music\\A",
                    "@Tempo": 120,
                    "@EndPosition": 1000,
                },
            ]
        }

        segments = list_music_segments(client)

        self.assertEqual(["A", "B"], [segment.name for segment in segments])
        self.assertIn("from type MusicSegment", client.call.call_args.args[1]["waql"])

    def test_reads_only_selected_music_segments(self) -> None:
        client = Mock()
        client.call.return_value = {
            "objects": [
                {
                    "id": "{SEGMENT}",
                    "name": "Segment",
                    "type": "MusicSegment",
                    "path": "\\Music\\Segment",
                    "@Tempo": 120,
                    "@EndPosition": 1000,
                },
                {"id": "{SOUND}", "name": "Sound", "type": "Sound"},
            ]
        }

        selected = get_selected_music_segments(client)

        self.assertEqual(["Segment"], [segment.name for segment in selected])
        self.assertEqual(WAAPI_UI_GET_SELECTED_OBJECTS, client.call.call_args_list[0].args[0])

    def test_selected_music_track_resolves_ancestor_segment_tempo(self) -> None:
        client = Mock()
        client.call.side_effect = [
            {
                "objects": [
                    {
                        "id": "{TRACK}",
                        "name": "Track",
                        "type": "MusicTrack",
                        "path": "\\Music\\Segment\\Track",
                    }
                ]
            },
            {
                "return": [
                    {
                        "id": "{SEGMENT}",
                        "name": "Segment",
                        "type": "MusicSegment",
                        "path": "\\Music\\Segment",
                        "@Tempo": 150,
                        "@EndPosition": 12800,
                    }
                ]
            },
        ]

        selected = get_selected_music_segments(client)

        self.assertEqual(1, len(selected))
        self.assertEqual(150.0, selected[0].tempo)
        ancestor_query = client.call.call_args_list[1].args[1]["waql"]
        self.assertIn("select ancestors", ancestor_query)
        self.assertIn('type = "MusicSegment"', ancestor_query)

    def test_execute_generation_is_one_undo_group_and_can_save(self) -> None:
        client = Mock()
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(CueSpec("Cue_0", 0.0, 0),),
            matching_cues=({"id": "{OLD}", "name": "Cue_old"},),
            conflicting_cues=(),
        )

        created = execute_generation(
            client,
            plan,
            replace_existing=True,
            batch_size=250,
            save=True,
        )

        self.assertEqual(1, created)
        uris = [item.args[0] for item in client.call.call_args_list]
        self.assertEqual(
            [WAAPI_UNDO_BEGIN, WAAPI_OBJECT_DELETE, WAAPI_OBJECT_SET, WAAPI_UNDO_END, WAAPI_PROJECT_SAVE],
            uris,
        )

    def test_execute_generation_rolls_back_on_failure(self) -> None:
        client = Mock()
        client.call.side_effect = [{}, RuntimeError("create failed"), {}]
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(CueSpec("Cue_0", 0.0, 0),),
            matching_cues=(),
            conflicting_cues=(),
        )

        with self.assertRaisesRegex(RuntimeError, "create failed"):
            execute_generation(
                client,
                plan,
                replace_existing=False,
                batch_size=250,
                save=False,
            )

        self.assertEqual(WAAPI_UNDO_CANCEL, client.call.call_args_list[-1].args[0])
        self.assertEqual({"undo": True}, client.call.call_args_list[-1].args[1])

    def test_loop_cue_is_prepared_from_content_end_and_excluded_from_event_prefix(self) -> None:
        client = Mock()
        client.call.side_effect = [
            {
                "return": [
                    {
                        "id": "{SEGMENT}",
                        "name": "Segment",
                        "type": "MusicSegment",
                        "path": "\\Music\\Segment",
                        "@Tempo": 120,
                        "@EndPosition": 1000,
                    }
                ]
            },
            {
                "return": [
                    {
                        "id": "{LOOP}",
                        "name": LOOP_CUE_NAME,
                        "type": "MusicCue",
                        "owner.id": "{SEGMENT}",
                        "@CueType": CUSTOM_CUE_TYPE,
                        "@TimeMs": 950,
                    },
                    {
                        "id": "{EXIT}",
                        "name": "Exit Cue",
                        "type": "MusicCue",
                        "owner.id": "{SEGMENT}",
                        "@CueType": 1,
                        "@TimeMs": 1000,
                    }
                ]
            },
            {
                "return": [
                    {
                        "id": "{CLIP}",
                        "name": "Clip",
                        "type": "MusicClip",
                        "path": "\\Music\\Segment\\Track\\Clip",
                        "@PlayAt": 100,
                        "@BeginTrimOffset": 50,
                        "@EndTrimOffset": 950,
                    }
                ]
            },
        ]

        plan = prepare_generation(
            client,
            segment_reference="\\Music\\Segment",
            interval_beats=Decimal("1"),
            start_beat=Decimal("0"),
            prefix="L",
            start_index=0,
            padding=0,
            include_end=False,
            max_cues=100,
            add_loop=True,
        )

        self.assertIsNotNone(plan.loop_spec)
        self.assertEqual(1000.0, plan.loop_spec.time_ms)
        self.assertEqual(1100.0, plan.target_end_ms)
        self.assertEqual(Decimal("1000.0"), plan.end_ms)
        self.assertEqual(("{LOOP}",), tuple(cue["id"] for cue in plan.existing_loop_cues))
        self.assertEqual((), plan.matching_cues)

    def test_loop_cue_replaces_existing_loop_without_replace_existing_option(self) -> None:
        client = Mock()
        def call_result(uri, args=None, **_kwargs):
            if uri != WAAPI_OBJECT_GET:
                return {}
            if args == {"from": {"id": ["{SEGMENT}"]}}:
                return {"return": [{"id": "{SEGMENT}", "@EndPosition": 1100}]}
            return {
                "return": [
                    {
                        "id": "{EXIT}",
                        "name": "Exit Cue",
                        "owner.id": "{SEGMENT}",
                        "@CueType": 1,
                        "@TimeMs": 1100,
                    }
                ]
            }

        client.call.side_effect = call_result
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(),
            matching_cues=(),
            conflicting_cues=(),
            loop_spec=CueSpec(LOOP_CUE_NAME, 1000.0, -1),
            existing_loop_cues=({"id": "{LOOP}", "name": LOOP_CUE_NAME},),
            target_end_ms=1100.0,
            exit_cues=({"id": "{EXIT}", "name": "Exit Cue", "@CueType": 1},),
        )

        created = execute_generation(
            client,
            plan,
            replace_existing=False,
            batch_size=250,
            save=False,
        )

        self.assertEqual(1, created)
        uris = [item.args[0] for item in client.call.call_args_list]
        self.assertEqual(
            [
                WAAPI_UNDO_BEGIN,
                WAAPI_OBJECT_SET_PROPERTY,
                WAAPI_OBJECT_SET_PROPERTY,
                WAAPI_OBJECT_DELETE,
                WAAPI_OBJECT_SET,
                WAAPI_OBJECT_GET,
                WAAPI_OBJECT_GET,
                WAAPI_UNDO_END,
            ],
            uris,
        )
        end_position_args = client.call.call_args_list[1].args[1]
        self.assertEqual("EndPosition", end_position_args["property"])
        self.assertEqual(1100.0, end_position_args["value"])
        exit_cue_args = client.call.call_args_list[2].args[1]
        self.assertEqual("{EXIT}", exit_cue_args["object"])
        self.assertEqual("TimeMs", exit_cue_args["property"])
        self.assertEqual(1100.0, exit_cue_args["value"])
        cue_payload = client.call.call_args_list[4].args[1]["objects"][0]["@Cues"]
        self.assertEqual(LOOP_CUE_NAME, cue_payload[0]["name"])
        self.assertEqual(1000.0, cue_payload[0]["@TimeMs"])

    def test_exit_cue_verification_failure_rolls_back_loop_update(self) -> None:
        client = Mock()
        def call_result(uri, args=None, **_kwargs):
            if uri != WAAPI_OBJECT_GET:
                return {}
            if args == {"from": {"id": ["{SEGMENT}"]}}:
                return {"return": [{"id": "{SEGMENT}", "@EndPosition": 1100}]}
            return {
                "return": [
                    {
                        "id": "{EXIT}",
                        "name": "Exit Cue",
                        "owner.id": "{SEGMENT}",
                        "@CueType": 1,
                        "@TimeMs": 1090,
                    }
                ]
            }

        client.call.side_effect = call_result
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(),
            matching_cues=(),
            conflicting_cues=(),
            loop_spec=CueSpec(LOOP_CUE_NAME, 1000.0, -1),
            target_end_ms=1100.0,
            exit_cues=({"id": "{EXIT}", "name": "Exit Cue", "@CueType": 1},),
        )

        with self.assertRaisesRegex(ToolError, "Exit Cue verification failed"):
            execute_generation(
                client,
                plan,
                replace_existing=False,
                batch_size=250,
                save=False,
            )

        self.assertEqual(WAAPI_UNDO_CANCEL, client.call.call_args_list[-1].args[0])

    def test_end_cursor_verification_failure_rolls_back_loop_update(self) -> None:
        client = Mock()
        client.call.side_effect = lambda uri, *_args, **_kwargs: (
            {"return": [{"id": "{SEGMENT}", "@EndPosition": 1090}]}
            if uri == WAAPI_OBJECT_GET
            else {}
        )
        plan = GenerationPlan(
            segment=SegmentInfo("{SEGMENT}", "Segment", "\\Music\\Segment", 120, 1000),
            tempo=Decimal("120"),
            end_ms=Decimal("1000"),
            specs=(),
            matching_cues=(),
            conflicting_cues=(),
            loop_spec=CueSpec(LOOP_CUE_NAME, 1000.0, -1),
            target_end_ms=1100.0,
            exit_cues=({"id": "{EXIT}", "name": "Exit Cue", "@CueType": 1},),
        )

        with self.assertRaisesRegex(ToolError, "End Cursor verification failed"):
            execute_generation(
                client,
                plan,
                replace_existing=False,
                batch_size=250,
                save=False,
            )

        self.assertEqual(WAAPI_UNDO_CANCEL, client.call.call_args_list[-1].args[0])

    def test_music_cues_are_queried_by_owner(self) -> None:
        client = Mock()
        client.call.return_value = {
            "return": [
                {"id": "{A}", "owner.id": "{SEGMENT}", "name": "Loop", "@CueType": 2},
                {"id": "{B}", "owner.id": "{OTHER}", "name": "Loop", "@CueType": 2},
            ]
        }

        cues = get_music_cues(client, "{SEGMENT}")

        self.assertEqual(["{A}"], [cue["id"] for cue in cues])
        self.assertIn("owner.id", client.call.call_args.args[1]["waql"])


if __name__ == "__main__":
    unittest.main()
