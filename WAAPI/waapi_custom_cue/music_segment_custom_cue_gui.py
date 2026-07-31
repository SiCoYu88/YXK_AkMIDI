#!/usr/bin/env python3
"""PySide6 GUI for generating indexed Wwise Music Segment Custom Cues."""

from __future__ import annotations

import asyncio
import json
import math
import os
import re
import socket
import sys
import threading
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from pathlib import Path
from typing import Any, Callable
from urllib.parse import urlparse

from PySide6.QtCore import QObject, QRunnable, QThreadPool, QTimer, Qt, Signal, Slot
from PySide6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QSplitter,
    QStatusBar,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from add_music_segment_custom_cues import (
    CueSpec,
    DEFAULT_WAAPI_URL,
    GenerationPlan,
    SegmentInfo,
    ToolError,
    execute_generation,
    get_selected_music_segments,
    list_music_segments,
    prepare_generation,
)


GUI_START_BEAT = Decimal("0")
GUI_INDEX_PADDING = 0


def application_directory() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


EVENT_TYPE_CONFIG_PATH = application_directory() / "custom_cue_event_types.json"
WAAPI_CONNECT_TIMEOUT_SECONDS = 2.0
WAAPI_SELECTION_CHANGED = "ak.wwise.ui.selectionChanged"
SELECTION_RECONNECT_SECONDS = 2.0


@dataclass(frozen=True)
class EventTypeConfig:
    event_type: str
    custom_cue_name: str


def load_event_type_config(path: Path = EVENT_TYPE_CONFIG_PATH) -> list[EventTypeConfig]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ToolError(f"事件类型配置文件不存在：{path}") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise ToolError(f"无法读取事件类型配置文件：{exc}") from exc

    raw_items = data.get("event_types") if isinstance(data, dict) else None
    if not isinstance(raw_items, list) or not raw_items:
        raise ToolError("事件类型配置必须包含非空的 event_types 数组。")

    items: list[EventTypeConfig] = []
    seen: set[str] = set()
    seen_cue_names: set[str] = set()
    for index, raw in enumerate(raw_items, start=1):
        if not isinstance(raw, dict):
            raise ToolError(f"event_types 第 {index} 项必须是对象。")
        event_type = raw.get("event_type")
        cue_name = raw.get("custom_cue_name")
        if not isinstance(event_type, str) or not event_type.strip():
            raise ToolError(f"event_types 第 {index} 项缺少有效的 event_type。")
        if not isinstance(cue_name, str) or not cue_name:
            raise ToolError(f"event_types 第 {index} 项缺少有效的 custom_cue_name。")
        event_type = event_type.strip()
        if event_type in seen:
            raise ToolError(f"事件类型重复：{event_type}")
        if cue_name in seen_cue_names:
            raise ToolError(f"Custom Cue 名称重复：{cue_name}")
        seen.add(event_type)
        seen_cue_names.add(cue_name)
        items.append(EventTypeConfig(event_type=event_type, custom_cue_name=cue_name))
    return items


@dataclass(frozen=True)
class GuiRequest:
    waapi_url: str
    segment_id: str
    event_type: str
    cue_name: str
    interval_beats: Decimal
    interval_label: str
    start_index: int
    include_end: bool
    replace_existing: bool
    save: bool
    add_loop: bool
    tempo_override: Decimal | None
    end_ms_override: Decimal | None


def build_preview_specs(plan: GenerationPlan) -> list[CueSpec]:
    specs = list(plan.specs)
    if plan.loop_spec:
        specs.append(plan.loop_spec)

    marker_time_ms = (
        plan.target_end_ms
        if plan.loop_spec and plan.target_end_ms is not None
        else plan.segment.end_ms
    )
    specs.append(CueSpec("End Cursor", float(marker_time_ms), -1))
    for cue in plan.exit_cues:
        exit_time_ms = marker_time_ms if plan.loop_spec else cue.get("@TimeMs")
        try:
            time_ms = float(exit_time_ms)
        except (TypeError, ValueError):
            continue
        if math.isfinite(time_ms) and time_ms >= 0:
            specs.append(CueSpec(str(cue.get("name") or "Exit Cue"), time_ms, -1))

    return sorted(specs, key=lambda spec: (spec.time_ms, spec.name))


def filter_music_segments(
    segments: list[SegmentInfo], query: str, *, use_regex: bool
) -> tuple[list[SegmentInfo], str | None]:
    query = query.strip()
    if not query:
        return list(segments), None

    if use_regex:
        try:
            pattern = re.compile(query, re.IGNORECASE)
        except re.error as exc:
            return [], str(exc)
        return [
            segment
            for segment in segments
            if pattern.search(f"{segment.name}\n{segment.path}")
        ], None

    tokens = query.casefold().split()
    return [
        segment
        for segment in segments
        if all(
            token in f"{segment.name}\n{segment.path}".casefold()
            for token in tokens
        )
    ], None


class WorkerSignals(QObject):
    result = Signal(object)
    error = Signal(object)
    finished = Signal()


class Worker(QRunnable):
    def __init__(self, work: Callable[[], Any]) -> None:
        super().__init__()
        self.work = work
        self.signals = WorkerSignals()

    @Slot()
    def run(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            result = self.work()
        except Exception as exc:
            self.signals.error.emit(exc)
        else:
            self.signals.result.emit(result)
        finally:
            loop.close()
            self.signals.finished.emit()


class SelectionSyncSignals(QObject):
    selection = Signal(object, object)
    state = Signal(object, bool, str)


class SelectionSyncService:
    def __init__(self, waapi_url: str) -> None:
        self.waapi_url = waapi_url
        self.signals = SelectionSyncSignals()
        self._stop_event = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="WwiseSelectionSync",
            daemon=True,
        )

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()

    def is_running(self) -> bool:
        return self._thread.is_alive() and not self._stop_event.is_set()

    def _run(self) -> None:
        try:
            from waapi import WaapiClient
        except ImportError as exc:
            self.signals.state.emit(self, False, str(exc))
            return

        while not self._stop_event.is_set():
            client = None
            try:
                asyncio.set_event_loop(asyncio.new_event_loop())
                client = WaapiClient(self.waapi_url, allow_exception=True)

                def selection_changed(*_args: Any, **_kwargs: Any) -> None:
                    if self._stop_event.is_set() or client is None:
                        return
                    try:
                        selected = get_selected_music_segments(client)
                    except Exception:
                        return
                    self.signals.selection.emit(self, selected)

                handler = client.subscribe(
                    WAAPI_SELECTION_CHANGED,
                    selection_changed,
                    {
                        "return": [
                            "id",
                            "name",
                            "type",
                            "path",
                            "@Tempo",
                            "@EndPosition",
                        ]
                    },
                )
                if handler is None:
                    raise ToolError("Wwise 未能建立选择同步订阅。")
                self.signals.state.emit(self, True, "")
                self.signals.selection.emit(self, get_selected_music_segments(client))

                while not self._stop_event.wait(0.25):
                    if not client.is_connected():
                        raise ToolError("Wwise WAAPI 连接已断开。")
            except Exception as exc:
                if not self._stop_event.is_set():
                    self.signals.state.emit(self, False, str(exc))
            finally:
                if client is not None:
                    try:
                        client.disconnect()
                    except Exception:
                        pass
                try:
                    loop = asyncio.get_event_loop()
                    if not loop.is_closed():
                        loop.close()
                except RuntimeError:
                    pass

            if not self._stop_event.is_set():
                self._stop_event.wait(SELECTION_RECONNECT_SECONDS)


class CustomCueWindow(QMainWindow):
    def __init__(self, *, suppress_dialogs: bool = False) -> None:
        super().__init__()
        self.setWindowTitle("Wwise Music Segment Custom Cue")
        self.resize(1120, 760)
        self.setMinimumSize(920, 620)

        self.busy = False
        self.suppress_dialogs = suppress_dialogs
        self.thread_pool = QThreadPool.globalInstance()
        self.workers: set[Worker] = set()
        self.segments: list[SegmentInfo] = []
        self.event_types: list[EventTypeConfig] = []
        self.selection_sync: SelectionSyncService | None = None
        self._pending_sync_selection: list[SegmentInfo] | None = None
        self._closing = False

        self._build_ui()
        self._reload_event_types(show_error=True)
        QTimer.singleShot(150, self.refresh_segments)

    def _build_ui(self) -> None:
        central = QWidget(self)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(12, 10, 12, 8)
        root_layout.setSpacing(10)
        self.setCentralWidget(central)

        connection = QWidget()
        connection_layout = QFormLayout(connection)
        connection_layout.setContentsMargins(0, 0, 0, 0)
        connection_layout.setHorizontalSpacing(10)
        connection_layout.setVerticalSpacing(8)

        url_row = QWidget()
        url_layout = QHBoxLayout(url_row)
        url_layout.setContentsMargins(0, 0, 0, 0)
        self.url_edit = QLineEdit(DEFAULT_WAAPI_URL)
        self.url_edit.editingFinished.connect(self._waapi_url_changed)
        self.refresh_button = QPushButton("刷新")
        self.refresh_button.clicked.connect(self.refresh_segments)
        self.selected_button = QPushButton("使用 Wwise 当前选择")
        self.selected_button.clicked.connect(self.use_wwise_selection)
        self.follow_selection_check = QCheckBox("跟随 Wwise 选择")
        self.follow_selection_check.setChecked(True)
        self.follow_selection_check.toggled.connect(self._follow_selection_toggled)
        url_layout.addWidget(self.url_edit, 1)
        url_layout.addWidget(self.refresh_button)
        url_layout.addWidget(self.selected_button)
        url_layout.addWidget(self.follow_selection_check)
        connection_layout.addRow("WAAPI", url_row)

        search_row = QWidget()
        search_layout = QHBoxLayout(search_row)
        search_layout.setContentsMargins(0, 0, 0, 0)
        search_layout.setSpacing(6)
        self.segment_search_edit = QLineEdit()
        self.segment_search_edit.setPlaceholderText("名称或完整路径")
        self.segment_search_edit.setClearButtonEnabled(True)
        self.segment_search_edit.textChanged.connect(self._segment_filter_changed)
        self.search_mode_group = QButtonGroup(self)
        self.search_mode_group.setExclusive(True)
        self.fuzzy_search_button = QPushButton("模糊")
        self.regex_search_button = QPushButton("正则")
        for mode_id, button in enumerate(
            (self.fuzzy_search_button, self.regex_search_button)
        ):
            button.setCheckable(True)
            button.setFixedSize(56, 28)
            self.search_mode_group.addButton(button, mode_id)
            button.toggled.connect(self._segment_filter_changed)
        self.fuzzy_search_button.blockSignals(True)
        self.fuzzy_search_button.setChecked(True)
        self.fuzzy_search_button.blockSignals(False)
        search_layout.addWidget(self.segment_search_edit, 1)
        search_layout.addWidget(self.fuzzy_search_button)
        search_layout.addWidget(self.regex_search_button)
        connection_layout.addRow("搜索", search_row)

        self.segment_combo = QComboBox()
        self.segment_combo.currentIndexChanged.connect(self._on_segment_selected)
        connection_layout.addRow("MusicSegment", self.segment_combo)
        self.segment_detail = QLabel("尚未连接 Wwise")
        self.segment_detail.setStyleSheet("color: #666666;")
        self.segment_detail.setTextInteractionFlags(Qt.TextSelectableByMouse)
        connection_layout.addRow("", self.segment_detail)
        root_layout.addWidget(connection)

        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        root_layout.addWidget(line)

        splitter = QSplitter(Qt.Horizontal)
        controls = self._build_controls()
        preview = self._build_preview()
        splitter.addWidget(controls)
        splitter.addWidget(preview)
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([350, 750])
        root_layout.addWidget(splitter, 1)

        status_bar = QStatusBar(self)
        self.setStatusBar(status_bar)
        self.status_label = QLabel("准备就绪")
        status_bar.addWidget(self.status_label, 1)
        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setFixedWidth(130)
        self.progress.hide()
        status_bar.addPermanentWidget(self.progress)

    def _build_controls(self) -> QWidget:
        panel = QWidget()
        panel.setMinimumWidth(320)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 4, 14, 4)
        layout.setSpacing(10)

        title = QLabel("生成参数")
        title.setStyleSheet("font-size: 15px; font-weight: 600;")
        layout.addWidget(title)

        form = QFormLayout()
        form.setHorizontalSpacing(12)
        form.setVerticalSpacing(10)

        self.interval_combo = QComboBox()
        self.interval_combo.setEditable(True)
        self.interval_combo.addItems(["1/8", "1/4", "1/2", "1", "2", "4"])
        self.interval_combo.setCurrentText("1/2")
        form.addRow("间隔（beat）", self.interval_combo)

        self.event_type_combo = QComboBox()
        self.event_type_combo.setToolTip(f"配置文件：{EVENT_TYPE_CONFIG_PATH}")
        form.addRow("事件类型", self.event_type_combo)

        self.start_index_spin = QSpinBox()
        self.start_index_spin.setRange(0, 999999)
        form.addRow("起始索引", self.start_index_spin)

        self.tempo_override_edit = QLineEdit()
        self.tempo_override_edit.setPlaceholderText("使用 MusicSegment BPM")
        form.addRow("覆盖 BPM（可选）", self.tempo_override_edit)
        self.end_override_edit = QLineEdit()
        self.end_override_edit.setPlaceholderText("使用 MusicSegment 结束位置")
        form.addRow("覆盖结束 ms（可选）", self.end_override_edit)
        layout.addLayout(form)

        self.include_end_check = QCheckBox("在结尾恰好命中时也添加 Cue")
        self.replace_check = QCheckBox("替换相同前缀的已有 Custom Cue")
        self.loop_check = QCheckBox("循环（Loop 位于内容末尾，End/Exit 延后 100 ms）")
        self.save_check = QCheckBox("执行后保存 Wwise 工程")
        self.replace_check.setChecked(True)
        self.loop_check.setChecked(True)
        self.save_check.setChecked(True)
        layout.addWidget(self.include_end_check)
        layout.addWidget(self.replace_check)
        layout.addWidget(self.loop_check)
        layout.addWidget(self.save_check)
        layout.addStretch(1)

        buttons = QHBoxLayout()
        self.preview_button = QPushButton("预览")
        self.preview_button.clicked.connect(self.preview_cues)
        self.execute_button = QPushButton("添加 Custom Cue")
        self.execute_button.setDefault(True)
        self.execute_button.clicked.connect(self.confirm_execute)
        buttons.addWidget(self.preview_button)
        buttons.addWidget(self.execute_button)
        layout.addLayout(buttons)
        return panel

    def _build_preview(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(14, 4, 0, 4)
        layout.setSpacing(8)
        title = QLabel("Cue 预览")
        title.setStyleSheet("font-size: 15px; font-weight: 600;")
        layout.addWidget(title)
        self.preview_summary = QLabel("选择 MusicSegment 后预览")
        self.preview_summary.setStyleSheet("color: #666666;")
        layout.addWidget(self.preview_summary)

        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["索引", "名称", "Beat", "时间 (ms)"])
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QTableWidget.SingleSelection)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.Stretch)
        header.setSectionResizeMode(2, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(3, QHeaderView.ResizeToContents)
        layout.addWidget(self.table, 1)
        return panel

    def closeEvent(self, event: Any) -> None:
        if self.busy or self.workers:
            answer = QMessageBox.question(
                self,
                "WAAPI 操作未结束",
                "当前 WAAPI 操作尚未结束，可能是 Wwise 已关闭或连接已断开。\n\n"
                "强制退出会立即终止工具。若 Wwise 仍在执行写入，请先确认工程状态。\n\n"
                "是否强制退出？",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No,
            )
            if answer != QMessageBox.Yes:
                event.ignore()
                return
            event.accept()
            self._closing = True
            self._stop_selection_sync()
            self._force_exit()
            return
        self._closing = True
        self._stop_selection_sync()
        event.accept()
        watchdog = threading.Timer(2.0, self._force_exit)
        watchdog.daemon = True
        watchdog.start()

    @staticmethod
    def _force_exit() -> None:
        os._exit(0)

    def _set_busy(self, busy: bool, text: str | None = None) -> None:
        self.busy = busy
        for button in (
            self.refresh_button,
            self.selected_button,
            self.preview_button,
            self.execute_button,
        ):
            button.setEnabled(not busy)
        for control in (
            self.url_edit,
            self.follow_selection_check,
            self.segment_search_edit,
            self.fuzzy_search_button,
            self.regex_search_button,
            self.segment_combo,
        ):
            control.setEnabled(not busy)
        self.progress.setVisible(busy)
        if text:
            self.status_label.setText(text)
        if (
            not busy
            and self._pending_sync_selection is not None
            and self.follow_selection_check.isChecked()
            and not self._closing
        ):
            selected = self._pending_sync_selection
            self._pending_sync_selection = None
            QTimer.singleShot(0, lambda: self._apply_deferred_selection(selected))

    def _waapi_url_changed(self) -> None:
        if self.follow_selection_check.isChecked():
            self._restart_selection_sync()

    def _apply_deferred_selection(self, selected: list[SegmentInfo]) -> None:
        if self.follow_selection_check.isChecked() and not self._closing:
            self._selection_loaded(selected, automatic=True)

    def _follow_selection_toggled(self, enabled: bool) -> None:
        if enabled:
            self._restart_selection_sync()
        else:
            self._pending_sync_selection = None
            self._stop_selection_sync()
            self.follow_selection_check.setToolTip("已暂停跟随 Wwise 选择")

    def _restart_selection_sync(self) -> None:
        if self.suppress_dialogs or self._closing:
            return
        waapi_url = self.url_edit.text().strip()
        if (
            self.selection_sync is not None
            and self.selection_sync.waapi_url == waapi_url
            and self.selection_sync.is_running()
        ):
            return
        self._stop_selection_sync()
        service = SelectionSyncService(waapi_url)
        service.signals.selection.connect(self._selection_sync_received)
        service.signals.state.connect(self._selection_sync_state_changed)
        self.selection_sync = service
        service.start()

    def _stop_selection_sync(self) -> None:
        service = self.selection_sync
        self.selection_sync = None
        if service is not None:
            service.stop()

    @Slot(object, object)
    def _selection_sync_received(
        self, service: SelectionSyncService, selected: list[SegmentInfo]
    ) -> None:
        if (
            service is not self.selection_sync
            or not self.follow_selection_check.isChecked()
            or self._closing
        ):
            return
        if self.busy:
            self._pending_sync_selection = selected
            return
        self._selection_loaded(selected, automatic=True)

    @Slot(object, bool, str)
    def _selection_sync_state_changed(
        self, service: SelectionSyncService, connected: bool, error: str
    ) -> None:
        if service is not self.selection_sync or self._closing:
            return
        if connected:
            self.follow_selection_check.setToolTip("已连接，正在跟随 Wwise 当前选择")
        else:
            self.follow_selection_check.setToolTip(
                "等待 Wwise WAAPI 连接，连接恢复后会自动继续同步"
                + (f"\n{error}" if error else "")
            )

    def _run_async(
        self,
        status: str,
        work: Callable[[], Any],
        on_success: Callable[[Any], None],
    ) -> None:
        if self.busy:
            return
        self._set_busy(True, status)
        worker = Worker(work)
        self.workers.add(worker)
        worker.signals.result.connect(lambda result: self._finish_success(result, on_success))
        worker.signals.error.connect(self._finish_error)
        worker.signals.finished.connect(lambda: self.workers.discard(worker))
        self.thread_pool.start(worker)

    def _finish_success(self, result: Any, callback: Callable[[Any], None]) -> None:
        self._set_busy(False)
        callback(result)

    def _finish_error(self, error: Exception) -> None:
        self._set_busy(False, "操作失败")
        message = self._friendly_error(error)
        self.status_label.setText(message)
        if not self.suppress_dialogs:
            QMessageBox.critical(self, "Wwise WAAPI", message)

    @staticmethod
    def _friendly_error(error: Exception) -> str:
        text = str(error)
        if isinstance(error, ToolError):
            return text
        if "ak.wwise.locked" in text:
            return "Wwise 正在等待一个对话框。请先在 Wwise 中关闭或确认该对话框。"
        if "ak.wwise.query.unknown_object" in text:
            return "目标 MusicSegment 已不存在，请刷新列表后重试。"
        if "Cannot connect" in text or error.__class__.__name__ == "CannotConnectToWaapiException":
            return "无法连接 Wwise。请启动 Wwise、打开工程并确认 WAAPI 已启用。"
        if error.__class__.__name__ == "WaapiRequestFailed":
            return f"WAAPI 请求失败：{text}"
        return f"{error.__class__.__name__}: {text}"

    @staticmethod
    def _with_client(waapi_url: str, operation: Callable[[Any], Any]) -> Any:
        try:
            from waapi import WaapiClient
        except ImportError as exc:
            raise ToolError("未安装 waapi-client，请先安装 requirements.txt 中的依赖。") from exc
        CustomCueWindow._ensure_waapi_endpoint(waapi_url)
        with WaapiClient(waapi_url, allow_exception=True) as client:
            return operation(client)

    @staticmethod
    def _ensure_waapi_endpoint(waapi_url: str) -> None:
        try:
            parsed = urlparse(waapi_url)
            if parsed.scheme not in ("ws", "wss") or not parsed.hostname:
                raise ValueError("invalid WAAPI URL")
            port = parsed.port or (443 if parsed.scheme == "wss" else 80)
        except ValueError as exc:
            raise ToolError(f"WAAPI 地址无效：{waapi_url}") from exc
        try:
            with socket.create_connection(
                (parsed.hostname, port),
                timeout=WAAPI_CONNECT_TIMEOUT_SECONDS,
            ):
                pass
        except OSError as exc:
            raise ToolError(
                f"无法连接 Wwise WAAPI（{parsed.hostname}:{port}）。"
                "请确认 Wwise 已启动、工程已打开并启用 WAAPI。"
            ) from exc

    def _reload_event_types(self, *, show_error: bool) -> bool:
        current = self._selected_event_type()
        current_name = current.event_type if current else None
        try:
            event_types = load_event_type_config()
        except ToolError as exc:
            self.event_types = []
            self.event_type_combo.clear()
            if show_error:
                QMessageBox.critical(self, "事件类型配置", str(exc))
            self.status_label.setText("事件类型配置读取失败")
            return False

        self.event_types = event_types
        self.event_type_combo.clear()
        for item in event_types:
            self.event_type_combo.addItem(
                f"{item.event_type}    [{item.custom_cue_name}0, {item.custom_cue_name}1, ...]",
                item,
            )
        index = next(
            (i for i, item in enumerate(event_types) if item.event_type == current_name),
            0,
        )
        self.event_type_combo.setCurrentIndex(index)
        return True

    def _selected_event_type(self) -> EventTypeConfig | None:
        value = self.event_type_combo.currentData()
        return value if isinstance(value, EventTypeConfig) else None

    def refresh_segments(self) -> None:
        if not self._reload_event_types(show_error=True):
            return
        if self.follow_selection_check.isChecked():
            self._restart_selection_sync()
        waapi_url = self.url_edit.text().strip()

        def work() -> tuple[list[SegmentInfo], list[SegmentInfo]]:
            return self._with_client(
                waapi_url,
                lambda client: (list_music_segments(client), get_selected_music_segments(client)),
            )

        self._run_async("正在读取 MusicSegment...", work, self._segments_loaded)

    def _segments_loaded(self, result: tuple[list[SegmentInfo], list[SegmentInfo]]) -> None:
        segments, selected = result
        current = self._selected_segment()
        current_id = current.object_id if current else None
        selected_id = (
            selected[0].object_id
            if self.follow_selection_check.isChecked() and len(selected) == 1
            else None
        )

        self.segments = segments
        target_id = selected_id or current_id
        if selected_id and self.segment_search_edit.text():
            self.segment_search_edit.blockSignals(True)
            self.segment_search_edit.clear()
            self.segment_search_edit.blockSignals(False)
        self._apply_segment_filter(preferred_id=target_id)
        self.status_label.setText(f"已连接 Wwise，共找到 {len(segments)} 个 MusicSegment")

    def _segment_filter_changed(self, *_args: Any) -> None:
        self._apply_segment_filter()

    def _apply_segment_filter(self, *, preferred_id: str | None = None) -> bool:
        current = self._selected_segment()
        current_id = current.object_id if current else None
        target_id = preferred_id or current_id
        matches, error = filter_music_segments(
            self.segments,
            self.segment_search_edit.text(),
            use_regex=self.regex_search_button.isChecked(),
        )
        if error:
            self.segment_search_edit.setStyleSheet("border: 1px solid #c62828;")
            self.segment_search_edit.setToolTip(f"正则表达式无效：{error}")
            self.status_label.setText(f"正则表达式无效：{error}")
            return False

        self.segment_search_edit.setStyleSheet("")
        self.segment_search_edit.setToolTip("")
        self.segment_combo.blockSignals(True)
        self.segment_combo.clear()
        for segment in matches:
            self.segment_combo.addItem(self._segment_display(segment), segment)
        target_index = next(
            (index for index, item in enumerate(matches) if item.object_id == target_id),
            0 if matches else -1,
        )
        self.segment_combo.setCurrentIndex(target_index)
        self.segment_combo.blockSignals(False)

        segment = self._selected_segment()
        new_id = segment.object_id if segment else None
        if segment and (new_id != current_id or preferred_id is not None):
            self._show_segment(segment)
        elif not segment:
            self.segment_detail.setText(
                "没有匹配的 MusicSegment" if self.segments else "当前工程没有 MusicSegment"
            )
            self.preview_summary.setText("选择 MusicSegment 后预览")
            self._clear_preview()
        if self.segment_search_edit.text():
            self.status_label.setText(
                f"搜索结果：{len(matches)} / {len(self.segments)} 个 MusicSegment"
            )
        return True

    @staticmethod
    def _segment_display(segment: SegmentInfo) -> str:
        return f"{segment.path}    [{segment.tempo:g} BPM, {segment.end_ms:g} ms]"

    def _selected_segment(self) -> SegmentInfo | None:
        value = self.segment_combo.currentData()
        return value if isinstance(value, SegmentInfo) else None

    @Slot(int)
    def _on_segment_selected(self, _index: int) -> None:
        segment = self._selected_segment()
        if segment:
            self._show_segment(segment)

    def _show_segment(self, segment: SegmentInfo) -> None:
        self.segment_detail.setText(
            f"{segment.name}  |  BPM {segment.tempo:g}  |  长度 {segment.end_ms:g} ms  |  {segment.object_id}"
        )
        self.preview_summary.setText("参数变化后请重新预览")
        self._clear_preview()

    def use_wwise_selection(self) -> None:
        waapi_url = self.url_edit.text().strip()
        self._run_async(
            "正在读取 Wwise 当前选择...",
            lambda: self._with_client(waapi_url, get_selected_music_segments),
            self._selection_loaded,
        )

    def _selection_loaded(
        self, selected: list[SegmentInfo], *, automatic: bool = False
    ) -> None:
        if not selected:
            if not automatic:
                QMessageBox.information(
                    self, "Wwise 当前选择", "请先在 Wwise 中选中一个 MusicSegment。"
                )
                self.status_label.setText("Wwise 当前未选择 MusicSegment")
            return
        if len(selected) > 1:
            if not automatic:
                QMessageBox.information(
                    self, "Wwise 当前选择", "当前选中了多个 MusicSegment，请只选择一个。"
                )
            self.status_label.setText("Wwise 当前选择包含多个 MusicSegment，已保留工具当前选择")
            return
        segment = selected[0]
        current = self._selected_segment()
        if current and current.object_id == segment.object_id:
            return

        existing_index = next(
            (
                index
                for index, item in enumerate(self.segments)
                if item.object_id == segment.object_id
            ),
            -1,
        )
        if existing_index >= 0:
            self.segments[existing_index] = segment
        else:
            self.segments.append(segment)
            self.segments.sort(key=lambda item: item.path.casefold())

        cleared_search = bool(self.segment_search_edit.text())
        if cleared_search:
            self.segment_search_edit.blockSignals(True)
            self.segment_search_edit.clear()
            self.segment_search_edit.blockSignals(False)
        self._apply_segment_filter(preferred_id=segment.object_id)
        self.status_label.setText(
            "已跟随 Wwise 当前选择"
            + ("，并清除搜索条件" if cleared_search else "")
            if automatic
            else "已使用 Wwise 当前选择"
        )

    @staticmethod
    def _parse_decimal(label: str, text: str, *, optional: bool, allow_zero: bool) -> Decimal | None:
        value_text = text.strip()
        if optional and not value_text:
            return None
        try:
            value = Decimal(value_text)
        except InvalidOperation as exc:
            raise ToolError(f"{label}必须是数字。") from exc
        minimum_ok = value >= 0 if allow_zero else value > 0
        if not value.is_finite() or not minimum_ok:
            rule = "大于等于 0" if allow_zero else "大于 0"
            raise ToolError(f"{label}必须{rule}。")
        return value

    @staticmethod
    def _parse_interval(text: str) -> tuple[Decimal, str]:
        value_text = text.strip()
        try:
            fraction = Fraction(value_text)
        except (ValueError, ZeroDivisionError) as exc:
            raise ToolError("间隔必须是正分数，例如 1/4、1/2、1 或 2。") from exc
        if fraction <= 0:
            raise ToolError("间隔必须大于 0。")
        value = Decimal(fraction.numerator) / Decimal(fraction.denominator)
        label = (
            str(fraction.numerator)
            if fraction.denominator == 1
            else f"{fraction.numerator}/{fraction.denominator}"
        )
        return value, label

    def _request(self) -> GuiRequest:
        segment = self._selected_segment()
        if segment is None:
            raise ToolError("请先选择一个 MusicSegment。")
        event_type = self._selected_event_type()
        if event_type is None:
            raise ToolError("请先在配置文件中设置并选择一个事件类型。")
        interval, interval_label = self._parse_interval(self.interval_combo.currentText())
        return GuiRequest(
            waapi_url=self.url_edit.text().strip(),
            segment_id=segment.object_id,
            event_type=event_type.event_type,
            cue_name=event_type.custom_cue_name,
            interval_beats=interval,
            interval_label=interval_label,
            start_index=self.start_index_spin.value(),
            include_end=self.include_end_check.isChecked(),
            replace_existing=self.replace_check.isChecked(),
            save=self.save_check.isChecked(),
            add_loop=self.loop_check.isChecked(),
            tempo_override=self._parse_decimal(
                "覆盖 BPM", self.tempo_override_edit.text(), optional=True, allow_zero=False
            ),
            end_ms_override=self._parse_decimal(
                "覆盖结束时间", self.end_override_edit.text(), optional=True, allow_zero=True
            ),
        )

    def _prepare_work(self, request: GuiRequest) -> GenerationPlan:
        return self._with_client(
            request.waapi_url,
            lambda client: prepare_generation(
                client,
                segment_reference=request.segment_id,
                interval_beats=request.interval_beats,
                start_beat=GUI_START_BEAT,
                prefix=request.cue_name,
                start_index=request.start_index,
                padding=GUI_INDEX_PADDING,
                include_end=request.include_end,
                max_cues=10000,
                tempo_override=request.tempo_override,
                end_ms_override=request.end_ms_override,
                add_loop=request.add_loop,
            ),
        )

    def preview_cues(self) -> None:
        try:
            request = self._request()
        except Exception as exc:
            self._finish_error(exc)
            return
        self._run_async(
            "正在计算并检查已有 Custom Cue...",
            lambda: self._prepare_work(request),
            lambda plan: self._preview_loaded(plan, request),
        )

    def _preview_loaded(self, plan: GenerationPlan, request: GuiRequest) -> None:
        self._show_plan(plan)
        conflict_text = f"，名称冲突 {len(plan.conflicting_cues)} 个" if plan.conflicting_cues else ""
        replace_text = (
            f"，将替换同前缀 Cue {len(plan.matching_cues)} 个"
            if request.replace_existing and plan.matching_cues
            else ""
        )
        loop_text = "，并添加/更新 Loop Cue" if plan.loop_spec else ""
        self.status_label.setText(
            f"预览完成：事件类型 {request.event_type}，将创建 {len(plan.specs)} 个事件 Cue"
            f"{loop_text}{replace_text}{conflict_text}"
        )

    def _show_plan(self, plan: GenerationPlan) -> None:
        specs = build_preview_specs(plan)
        visible: list[Any] = specs
        if len(specs) > 2000:
            visible = specs[:1000] + [None] + specs[-1000:]
        self.table.setUpdatesEnabled(False)
        self.table.setRowCount(len(visible))
        beat_ms = 60000.0 / float(plan.tempo)
        for row, spec in enumerate(visible):
            if spec is None:
                values = ("...", "...", "...", "...")
            else:
                beat = spec.time_ms / beat_ms
                index_text = "-" if spec.index < 0 else str(spec.index)
                values = (index_text, spec.name, f"{beat:.6g}", f"{spec.time_ms:.3f}")
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                if column in (0, 2, 3):
                    item.setTextAlignment(Qt.AlignRight | Qt.AlignVCenter)
                self.table.setItem(row, column, item)
        self.table.setUpdatesEnabled(True)
        end_cursor_ms = (
            plan.target_end_ms
            if plan.loop_spec and plan.target_end_ms is not None
            else plan.segment.end_ms
        )
        exit_times: list[float] = []
        for cue in plan.exit_cues:
            raw_time_ms = plan.target_end_ms if plan.loop_spec else cue.get("@TimeMs")
            try:
                exit_time_ms = float(raw_time_ms)
            except (TypeError, ValueError):
                continue
            if math.isfinite(exit_time_ms) and exit_time_ms >= 0:
                exit_times.append(exit_time_ms)
        exit_text = (
            "  |  Exit Cue "
            + ", ".join(f"{time_ms:.3f} ms" for time_ms in exit_times)
            if exit_times
            else ""
        )
        loop_text = (
            f"  |  Loop {plan.loop_spec.time_ms:.3f} ms" if plan.loop_spec else ""
        )
        self.preview_summary.setText(
            f"{len(plan.specs)} 个事件 Cue  |  {plan.tempo} BPM  |  事件截止 {plan.end_ms} ms"
            f"{loop_text}  |  End Cursor {end_cursor_ms:.3f} ms{exit_text}"
            f"  |  已有同前缀 {len(plan.matching_cues)} 个"
        )

    def _clear_preview(self) -> None:
        self.table.setRowCount(0)

    def confirm_execute(self) -> None:
        try:
            request = self._request()
        except Exception as exc:
            self._finish_error(exc)
            return
        self._run_async(
            "执行前正在检查 MusicSegment...",
            lambda: self._prepare_work(request),
            lambda plan: self._confirm_plan(plan, request),
        )

    def _confirm_plan(self, plan: GenerationPlan, request: GuiRequest) -> None:
        self._show_plan(plan)
        if plan.conflicting_cues and not request.replace_existing:
            QMessageBox.critical(
                self,
                "存在名称冲突",
                f"已有 {len(plan.conflicting_cues)} 个同名 Cue。请启用“替换相同前缀”或修改前缀/索引。",
            )
            self.status_label.setText("未执行：存在 Cue 名称冲突")
            return

        replace_count = len(plan.matching_cues) if request.replace_existing else 0
        loop_text = (
            f"添加 Loop Cue：是（{plan.loop_spec.time_ms:.3f} ms，"
            f"End Cursor/Exit Cue：{plan.target_end_ms:.3f} ms，"
            f"更新已有 Loop {len(plan.existing_loop_cues)} 个）"
            if plan.loop_spec and plan.target_end_ms is not None
            else "添加 Loop Cue：否"
        )
        save_text = "执行后立即保存工程" if request.save else "执行后不自动保存，可在 Wwise 中一次撤销"
        message = (
            f"MusicSegment：\n{plan.segment.path}\n\n"
            f"事件类型：{request.event_type}\n"
            f"Cue 名称：{request.cue_name}0, {request.cue_name}1, ...\n"
            f"新增事件 Cue：{len(plan.specs)} 个\n"
            f"删除并替换同类型 Cue：{replace_count} 个\n"
            f"{loop_text}\n"
            f"间隔：{request.interval_label} beat\n"
            f"{save_text}\n\n确认执行？"
        )
        answer = QMessageBox.question(
            self,
            "确认添加 Custom Cue",
            message,
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if answer != QMessageBox.Yes:
            self.status_label.setText("已取消执行")
            return

        signature = self._plan_signature(plan)
        self._run_async(
            "正在通过 WAAPI 添加 Custom Cue...",
            lambda: self._execute_work(request, signature),
            lambda result: self._execution_finished(result, request),
        )

    @staticmethod
    def _plan_signature(plan: GenerationPlan) -> tuple[Any, ...]:
        return (
            plan.segment.object_id,
            tuple((spec.name, spec.time_ms) for spec in plan.specs),
            tuple(cue.get("id") for cue in plan.matching_cues),
            (plan.loop_spec.name, plan.loop_spec.time_ms) if plan.loop_spec else None,
            tuple(cue.get("id") for cue in plan.existing_loop_cues),
            plan.target_end_ms,
            tuple((cue.get("id"), cue.get("@TimeMs")) for cue in plan.exit_cues),
        )

    def _execute_work(
        self,
        request: GuiRequest,
        confirmed_signature: tuple[Any, ...],
    ) -> tuple[int, int, bool, int]:
        def operation(client: Any) -> tuple[int, int, bool, int]:
            plan = prepare_generation(
                client,
                segment_reference=request.segment_id,
                interval_beats=request.interval_beats,
                start_beat=GUI_START_BEAT,
                prefix=request.cue_name,
                start_index=request.start_index,
                padding=GUI_INDEX_PADDING,
                include_end=request.include_end,
                max_cues=10000,
                tempo_override=request.tempo_override,
                end_ms_override=request.end_ms_override,
                add_loop=request.add_loop,
            )
            if self._plan_signature(plan) != confirmed_signature:
                raise ToolError("确认后 Wwise 数据发生了变化。为避免误操作，请重新预览并确认。")
            created = execute_generation(
                client,
                plan,
                replace_existing=request.replace_existing,
                batch_size=250,
                save=request.save,
            )
            replaced = len(plan.matching_cues) if request.replace_existing else 0
            return len(plan.specs), replaced, plan.loop_spec is not None, len(plan.existing_loop_cues)

        return self._with_client(request.waapi_url, operation)

    def _execution_finished(self, result: tuple[int, int, bool, int], request: GuiRequest) -> None:
        created, replaced, loop_created, loop_replaced = result
        save_text = "工程已保存。" if request.save else "工程尚未保存，可在 Wwise 中一次撤销全部操作。"
        loop_text = (
            f"\nLoop Cue 已添加，更新已有 {loop_replaced} 个。" if loop_created else ""
        )
        QMessageBox.information(
            self,
            "Custom Cue 添加完成",
            f"已创建 {created} 个事件 Cue。\n已替换 {replaced} 个同类型 Cue。"
            f"{loop_text}\n\n{save_text}",
        )
        self.status_label.setText(
            f"执行完成：创建 {created} 个事件 Cue，替换 {replaced} 个"
            + ("，Loop Cue 已更新" if loop_created else "")
        )


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    smoke_test = "--smoke-test" in arguments
    if smoke_test:
        import waapi  # noqa: F401 - verifies that the packaged WAAPI client can be imported.

    app = QApplication(sys.argv)
    app.setApplicationName("Wwise Music Segment Custom Cue")
    app.setStyle("Fusion")
    window = CustomCueWindow(suppress_dialogs=smoke_test)
    if smoke_test:
        QTimer.singleShot(4000, lambda: os._exit(0 if window.event_type_combo.count() > 0 else 2))
    else:
        window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
