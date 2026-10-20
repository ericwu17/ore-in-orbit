// ui_stats.cpp — Dear ImGui / ImPlot presentation of the simulation's
// statistics (stats.h): the Factorio-style statistics window, and the chart
// building blocks it is made of (time ranges, rate / level / average lines
// with a hover readout, sparklines, sortable tables, summary tiles).
// Read-only towards the World.
#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/ui_theme.h"

#include "sim/game_types.h"
#include "sim/stats.h"
#include "sim/world.h"
#include "imgui.h"
#include "implot.h"
#include "implot_internal.h"  // ImPlotAxis, for PadYFit

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Time ranges
// ---------------------------------------------------------------------------

// The range buttons. Each shows the newest `buckets` buckets of one stat
// level (stats.h), with x-axis ticks every `tick_step`.
struct StatRange {
    const char* label;
    std::size_t level;
    std::size_t buckets;
    Tick tick_step;
    bool day_labels;  // label ticks "Day N" rather than with the time of day
};

constexpr StatRange kStatRanges[] = {
    {"6 h", 0, 30, kTicksPerHour, false},
    {"1 day", 0, kStatBuckets, 3 * kTicksPerHour, false},
    {"10 days", 1, kStatBuckets, kTicksPerDay, true},
    {"30 days", 2, kStatBuckets, 5 * kTicksPerDay, true},
};
constexpr int kStatRangeCount = static_cast<int>(sizeof(kStatRanges) / sizeof(kStatRanges[0]));

// Index into kStatRanges, shared by every stats view. A viewing preference,
// so unlike the rest of the window state (ui::g_game) it carries over between games.
int g_range = 1;

const StatRange& CurrentRange() { return kStatRanges[g_range]; }
Tick RangeTicks(const StatRange& r) { return kStatBucketTicks[r.level] * r.buckets; }
double TicksToHours(Tick ticks) { return static_cast<double>(ticks) / static_cast<double>(kTicksPerHour); }

// A series' total over the current range, the bucket in progress included.
int64_t RangeSum(const StatSeries& series, Tick now) {
    const StatRange& r = CurrentRange();
    return series.sum(r.level, now, r.buckets);
}

// A gauge's peak over the current range.
int64_t RangePeak(const StatSeries& series, Tick now) {
    const StatRange& r = CurrentRange();
    std::array<int64_t, kStatBuckets> values{};
    series.read(r.level, now, r.buckets, values.data());
    return *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(r.buckets));
}

// The range picker: a segmented control, the current range filled.
void DrawRangeButtons() {
    for (int i = 0; i < kStatRangeCount; ++i) {
        if (i > 0)
            ImGui::SameLine(0.0f, 2.0f);
        if (ui::theme::SegmentedButton(kStatRanges[i].label, g_range == i))
            g_range = i;
    }
}

// ---------------------------------------------------------------------------
// Value formatting (literal format strings only: -Wformat=2)
// ---------------------------------------------------------------------------

enum class Unit { Count, PerHour, Money, MoneyPerHour, Hours, Percent };

void FormatValue(Unit unit, double v, char* buf, std::size_t len) {
    switch (unit) {
    case Unit::Count:        std::snprintf(buf, len, "%.0f", v); return;
    case Unit::PerHour:      std::snprintf(buf, len, "%.1f /h", v); return;
    case Unit::Money:        std::snprintf(buf, len, "$%.0f", v); return;
    case Unit::MoneyPerHour: std::snprintf(buf, len, "$%.0f /h", v); return;
    case Unit::Hours:        std::snprintf(buf, len, "%.1f h", v); return;
    case Unit::Percent:      std::snprintf(buf, len, "%.0f%%", v); return;
    }
}

// ---------------------------------------------------------------------------
// Time charts
// ---------------------------------------------------------------------------

// The buckets one series shows in a chart of the current range.
struct ChartWindow {
    std::size_t level = 0;
    std::size_t count = 0;
    Tick end = 0;     // a tick inside the newest bucket shown
    Tick bucket = 0;  // bucket length in ticks
};

// Flows stop at the last completed bucket, so the bucket still filling up
// never reads as a sudden drop; gauges include it (their value is a peak).
ChartWindow WindowFor(StatKind kind, Tick now) {
    const StatRange& r = CurrentRange();
    const Tick bucket = kStatBucketTicks[r.level];
    const Tick end = kind == StatKind::Flow && now >= bucket ? now - bucket : now;
    return {r.level, r.buckets, end, bucket};
}

// Each bucket is plotted at the middle of its time span, in hours since the
// start of the game (the x unit of every time chart).
std::vector<double> BucketXs(const ChartWindow& w) {
    std::vector<double> xs(w.count);
    const double newest = static_cast<double>(w.end / w.bucket);
    for (std::size_t i = 0; i < w.count; ++i) {
        const double bucket = newest - static_cast<double>(w.count - 1 - i);
        xs[i] = (bucket + 0.5) * TicksToHours(w.bucket);
    }
    return xs;
}

std::vector<double> BucketValues(const StatSeries& series, const ChartWindow& w) {
    std::vector<int64_t> raw(w.count);
    series.read(w.level, w.end, w.count, raw.data());
    return std::vector<double>(raw.begin(), raw.end());
}

// Everything plotted in the current chart, for the hover readout.
struct ReadoutSeries {
    std::string label;
    Unit unit;
    std::vector<double> xs, ys;
};
std::vector<ReadoutSeries> g_readout;

void PlotAndRecord(const char* label, Unit unit, std::vector<double> xs, std::vector<double> ys,
                   const ImPlotSpec& spec) {
    ImPlot::PlotLine(label, xs.data(), ys.data(), static_cast<int>(xs.size()), spec);
    g_readout.push_back({label, unit, std::move(xs), std::move(ys)});
}

// Chart lines' thickness, in pixels.
constexpr float kLineWeight = 2.0f;

ImPlotSpec LineSpec(std::optional<ImVec4> color = std::nullopt) {
    ImPlotSpec spec;
    spec.LineWeight = kLineWeight;
    spec.Flags = ImPlotLineFlags_SkipNaN;
    if (color.has_value())
        spec.LineColor = *color;
    return spec;
}

// Flows, summed, as a rate per in-game hour: the rate reads the same
// whichever range (and so bucket length) is shown.
void PlotRate(const char* label, std::initializer_list<const StatSeries*> series, Tick now,
              Unit unit = Unit::PerHour, std::optional<ImVec4> color = std::nullopt) {
    const ChartWindow w = WindowFor(StatKind::Flow, now);
    std::vector<double> ys(w.count, 0.0);
    for (const StatSeries* s : series) {
        const std::vector<double> values = BucketValues(*s, w);
        for (std::size_t i = 0; i < w.count; ++i)
            ys[i] += values[i];
    }
    const double per_hour = 1.0 / TicksToHours(w.bucket);
    for (double& y : ys)
        y *= per_hour;
    PlotAndRecord(label, unit, BucketXs(w), std::move(ys), LineSpec(color));
}

// A gauge's per-bucket peak, times `scale`.
void PlotLevel(const char* label, const StatSeries& series, Tick now, Unit unit,
               double scale = 1.0, std::optional<ImVec4> color = std::nullopt) {
    const ChartWindow w = WindowFor(StatKind::Gauge, now);
    std::vector<double> ys = BucketValues(series, w);
    for (double& y : ys)
        y *= scale;
    PlotAndRecord(label, unit, BucketXs(w), std::move(ys), LineSpec(color));
}

// Per bucket, sum(numerator) / sum(denominators) * scale: e.g. total waiting
// time over items processed. Buckets with a zero denominator are gaps.
void PlotAverage(const char* label, const StatSeries& numerator,
                 std::initializer_list<const StatSeries*> denominators, Tick now, Unit unit,
                 double scale, std::optional<ImVec4> color = std::nullopt) {
    const ChartWindow w = WindowFor(StatKind::Flow, now);
    std::vector<double> ys = BucketValues(numerator, w);
    std::vector<double> counts(w.count, 0.0);
    for (const StatSeries* s : denominators) {
        const std::vector<double> values = BucketValues(*s, w);
        for (std::size_t i = 0; i < w.count; ++i)
            counts[i] += values[i];
    }
    for (std::size_t i = 0; i < w.count; ++i)
        ys[i] = counts[i] > 0.0 ? ys[i] / counts[i] * scale : std::numeric_limits<double>::quiet_NaN();
    PlotAndRecord(label, unit, BucketXs(w), std::move(ys), LineSpec(color));
}

// X-axis tick labels: hours since the start of the game, shown as a time of
// day ("06:00"), or "Day N" at midnight and on the multi-day ranges.
int FormatAxisTime(double hours, char* buf, int size, void* /*user_data*/) {
    const StatRange* range = &CurrentRange();
    const Tick tick = static_cast<Tick>(std::llround(std::max(hours, 0.0) * static_cast<double>(kTicksPerHour)));
    const ClockTime t = tick_to_time_of_day(tick);
    const auto day = static_cast<unsigned long long>(tick / kTicksPerDay) + 1;
    const auto len = static_cast<std::size_t>(size);
    if (range->day_labels || (t.hour == 0 && t.minute == 0))
        return std::snprintf(buf, len, "Day %llu", day);
    return std::snprintf(buf, len, "%02d:%02d", t.hour, t.minute);
}

// Widens the y axis's auto-fit by one line weight's worth of pixels above and
// below the data, so a line at the data's max or min isn't half clipped by the
// plot edge, with no further empty space. Call just before ImPlot::EndPlot,
// which applies the fit: by then every item has been measured. Flat data is
// shown one unit tall, resting on zero when it is near zero.
void PadYFit() {
    ImPlotAxis& y = ImPlot::GetCurrentPlot()->Axes[ImAxis_Y1];
    if (!y.FitThisFrame || !(y.FitExtents.Min <= y.FitExtents.Max))  // nothing plotted
        return;
    double lo = y.FitExtents.Min;
    double hi = y.FitExtents.Max;
    if (hi - lo < 1e-9) {
        lo = std::max(lo - 0.5, std::min(lo, 0.0));
        hi = lo + 1.0;
    }
    // Solve margin / (range + 2 * margin) = kLineWeight / plot height.
    const double usable_px = static_cast<double>(y.PixelSize() - 2.0f * kLineWeight);
    if (usable_px <= 0.0)
        return;
    const double margin = (hi - lo) * static_cast<double>(kLineWeight) / usable_px;
    y.FitExtents.Min = lo - margin;
    y.FitExtents.Max = hi + margin;
}

// Opens a chart over the current range, scrolling with the game clock (the x
// axis always ends at `now`). Plot series with PlotRate / PlotLevel /
// PlotAverage, then close with EndTimeChart, which adds the hover readout.
bool BeginTimeChart(const char* title, Tick now, const char* y_label, float height) {
    g_readout.clear();
    const float chart_width = ImGui::GetContentRegionAvail().x;
    if (!ImPlot::BeginPlot(title, ImVec2(-1.0f, height), ImPlotFlags_NoMouseText | ImPlotFlags_NoBoxSelect))
        return false;
    const StatRange& r = CurrentRange();
    const Tick span = RangeTicks(r);
    const Tick start = now > span ? now - span : 0;
    const double x_min = TicksToHours(start);
    const double x_max = std::max(TicksToHours(now), x_min + 0.5);

    ImPlot::SetupAxes(nullptr, y_label, ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
    ImPlot::SetupAxisFormat(ImAxis_X1, FormatAxisTime);
    // Ticks on round times: every tick_step since the start of the game,
    // thinned to every 2nd, 3rd... step until the labels fit the chart.
    const float label_width = ImGui::CalcTextSize("Day 00  ").x;
    const auto max_labels = static_cast<Tick>(std::max(2.0f, chart_width / label_width * 0.8f));
    Tick step = r.tick_step;
    while (span / step > max_labels)
        step += r.tick_step;
    std::vector<double> ticks;
    for (Tick t = (start + step - 1) / step * step; t <= now; t += step)
        ticks.push_back(TicksToHours(t));
    if (!ticks.empty())
        ImPlot::SetupAxisTicks(ImAxis_X1, ticks.data(), static_cast<int>(ticks.size()));
    ImPlot::SetupLegend(ImPlotLocation_NorthWest);
    return true;
}

// Vertical markers at the given ticks (e.g. midnights, or player edits). Call between
// BeginTimeChart and EndTimeChart.
void PlotTickMarkers(const char* label, const std::vector<Tick>& ticks, ImVec4 color) {
    std::vector<double> xs;
    for (Tick t : ticks)
        xs.push_back(TicksToHours(t));
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = 1.5f;
    ImPlot::PlotInfLines(label, xs.data(), static_cast<int>(xs.size()), spec);
}

// Hovering a chart shows every series' value in the bucket under the mouse.
void EndTimeChart() {
    if (ImPlot::IsPlotHovered() && !g_readout.empty() && !g_readout.front().xs.empty()) {
        const double mouse_x = ImPlot::GetPlotMousePos().x;
        const std::vector<double>& xs = g_readout.front().xs;
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < xs.size(); ++i)
            if (std::abs(xs[i] - mouse_x) < std::abs(xs[nearest] - mouse_x))
                nearest = i;
        ImGui::BeginTooltip();
        char when[32];
        ui::FormatDayTime(static_cast<Tick>(std::max(xs[nearest], 0.0) * static_cast<double>(kTicksPerHour)),
                          when, sizeof(when));
        ImGui::TextDisabled("Around %s", when);
        for (const ReadoutSeries& s : g_readout) {
            if (nearest >= s.ys.size())
                continue;
            char value[48];
            if (std::isnan(s.ys[nearest]))
                std::snprintf(value, sizeof(value), "-");
            else
                FormatValue(s.unit, s.ys[nearest], value, sizeof(value));
            ImGui::Text("%s: %s", s.label.c_str(), value);
        }
        ImGui::EndTooltip();
    }
    PadYFit();
    ImPlot::EndPlot();
}

// A small, axis-less line of `values` for table cells.
void Sparkline(const char* id, const std::vector<double>& values, double y_max, ImVec2 size, ImVec4 color) {
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0.0f, 0.0f));
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs | ImPlotFlags_NoFrame)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        // 15% headroom, so a line pinned at y_max (a full buffer, the
        // range's peak) stays visible instead of merging into the top edge, and a
        // pixel of footroom (the default line weight) so a line at zero isn't
        // half clipped by the bottom.
        const double top = std::max(y_max, 1.0) * 1.15;
        const double bottom = -top / std::max(static_cast<double>(size.y) - 1.0, 1.0);
        ImPlot::SetupAxesLimits(0.0, std::max(1.0, static_cast<double>(values.size()) - 1.0), bottom,
                                top, ImPlotCond_Always);
        ImPlotSpec spec;
        spec.LineColor = color;
        spec.FillColor = color;
        spec.FillAlpha = 0.25f;
        ImPlot::PlotShaded(id, values.data(), static_cast<int>(values.size()), 0.0, 1.0, 0.0, spec);
        ImPlot::PlotLine(id, values.data(), static_cast<int>(values.size()), 1.0, 0.0, spec);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}

// Semantic series colors, from the shared palette (ui_theme.h); the rest
// cycle through the theme's "Postal" colormap.
constexpr ImVec4 kGood = ui::theme::kGood;         // good outcomes
constexpr ImVec4 kWarn = ui::theme::kWarn;         // things piling up (lines and bars)
constexpr ImVec4 kNeutral = ui::theme::kNavy;      // counts, levels
constexpr ImVec4 kMuted = ui::theme::kInkDim;      // secondary lines, markers

// Sorts table rows by the active column of a sortable ImGui table; `key`
// maps (row, column index) to a comparable number.
template <typename Row, typename Key>
void SortRows(std::vector<Row>& rows, Key key) {
    const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || specs->SpecsCount == 0)
        return;
    const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
    const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        const double ka = key(a, spec.ColumnIndex);
        const double kb = key(b, spec.ColumnIndex);
        return ascending ? ka < kb : ka > kb;
    });
}

// A summary tile: a small label over a big value, in an inset paper box.
// Footnote lines go between BeginStatTile and EndStatTile.
void BeginStatTile(const char* id, const char* label, const char* value, ImVec4 value_color) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPaper2);
    ImGui::BeginChild(id, ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(nullptr, ui::theme::kFontSmall);
    ImGui::TextDisabled("%s", label);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, ui::theme::kFontTitle * 1.2f);
    ImGui::TextColored(value_color, "%s", value);
    ImGui::PopFont();
}

void EndStatTile() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Overview tab: an example of every building block above, charting the
// placeholder simulation's GlobalStats. Replace with tabs of your own.
// ---------------------------------------------------------------------------

// Every midnight in the current range, for PlotTickMarkers.
std::vector<Tick> MidnightsInRange(Tick now) {
    const Tick span = RangeTicks(CurrentRange());
    const Tick start = now > span ? now - span : 0;
    std::vector<Tick> ticks;
    for (Tick t = (start + kTicksPerDay - 1) / kTicksPerDay * kTicksPerDay; t <= now; t += kTicksPerDay)
        if (t > 0)
            ticks.push_back(t);
    return ticks;
}

// One row per metric in the catalogue: its range total (flows) or peak
// (gauges), and a sparkline.
void DrawMetricsTable(const GlobalStats& g, Tick now) {
    struct Row {
        GlobalStat metric;
        const StatInfo* info;
        int64_t value;
    };
    std::vector<Row> rows;
    const std::span<const StatInfo> infos = stat_infos(GlobalStat{});
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto m = static_cast<GlobalStat>(i);
        rows.push_back({m, &infos[i], infos[i].kind == StatKind::Flow ? RangeSum(g[m], now) : RangePeak(g[m], now)});
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable |
                                  ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("metrics", 4, flags))
        return;
    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_NoSort);
    ImGui::TableSetupColumn("Total / peak");
    ImGui::TableSetupColumn("Trend", ImGuiTableColumnFlags_NoSort, 140.0f);
    ImGui::TableHeadersRow();
    SortRows(rows, [](const Row& r, int column) -> double {
        switch (column) {
        case 0: return static_cast<double>(r.metric);
        case 2: return static_cast<double>(r.value);
        default: return 0.0;
        }
    });
    for (const Row& r : rows) {
        ImGui::PushID(static_cast<int>(r.metric));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.info->key);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", r.info->kind == StatKind::Flow ? "flow" : "gauge");
        ImGui::TableNextColumn();
        ImGui::Text("%lld", static_cast<long long>(r.value));
        ImGui::TableNextColumn();
        const ChartWindow w = WindowFor(r.info->kind, now);
        const std::vector<double> values = BucketValues(g[r.metric], w);
        const double y_max = values.empty() ? 1.0 : *std::max_element(values.begin(), values.end());
        Sparkline("##trend", values, y_max, ImVec2(-1.0f, ImGui::GetTextLineHeight()), kNeutral);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DrawOverviewTab(const World& world) {
    const GlobalStats& g = world.get_stats();
    const Tick now = world.get_tick();
    const int64_t arrived = RangeSum(g[GlobalStat::Arrived], now);
    const int64_t processed = RangeSum(g[GlobalStat::Processed], now);

    // Summary for the range: one tile per headline number.
    if (ImGui::BeginTable("summary", 3, ImGuiTableFlags_SizingStretchSame)) {
        char value[48];
        ImGui::TableNextColumn();
        std::snprintf(value, sizeof(value), "%lld", static_cast<long long>(arrived));
        BeginStatTile("arrived", "Arrived", value, ui::theme::kInk);
        ImGui::TextDisabled("items queued");
        EndStatTile();

        ImGui::TableNextColumn();
        std::snprintf(value, sizeof(value), "%lld", static_cast<long long>(processed));
        BeginStatTile("processed", "Processed", value, kGood);
        if (arrived > 0)
            ImGui::TextDisabled("%.0f%% of arrivals", 100.0 * static_cast<double>(processed) / static_cast<double>(arrived));
        else
            ImGui::TextDisabled("nothing yet");
        EndStatTile();

        ImGui::TableNextColumn();
        std::snprintf(value, sizeof(value), "%lld", static_cast<long long>(RangePeak(g[GlobalStat::Queue], now)));
        BeginStatTile("peak", "Peak queue", value, ui::theme::kWarnText);
        ImGui::TextDisabled("items waiting at once");
        EndStatTile();
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // Charts, two per row.
    const float chart_h = 190.0f;
    if (ImGui::BeginTable("charts", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (BeginTimeChart("Throughput", now, "per hour", chart_h)) {
            PlotRate("Arrived", {&g[GlobalStat::Arrived]}, now, Unit::PerHour, kNeutral);
            PlotRate("Processed", {&g[GlobalStat::Processed]}, now, Unit::PerHour, kGood);
            PlotTickMarkers("Midnight", MidnightsInRange(now), kMuted);
            EndTimeChart();
        }
        ImGui::TableNextColumn();
        if (BeginTimeChart("Queue", now, "items", chart_h)) {
            PlotLevel("Waiting", g[GlobalStat::Queue], now, Unit::Count, 1.0, kWarn);
            EndTimeChart();
        }
        ImGui::TableNextColumn();
        if (BeginTimeChart("Processed share", now, "% of arrivals", chart_h)) {
            PlotAverage("Processed", g[GlobalStat::Processed], {&g[GlobalStat::Arrived]}, now, Unit::Percent,
                        100.0, kGood);
            EndTimeChart();
        }
        ImGui::TableNextColumn();
        DrawMetricsTable(g, now);
        ImGui::EndTable();
    }
}

// The statistics window's contents: the range buttons and the tabs.
void DrawStatsContents(const World& world) {
    DrawRangeButtons();
    ImGui::SameLine();
    ImGui::TextDisabled("  of in-game time");
    if (ImGui::BeginTabBar("stats_tabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            DrawOverviewTab(world);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void DrawStatsWindow(const World& world) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_L, false))
        ui::g_game.stats_open = !ui::g_game.stats_open;
    if (!ui::g_game.stats_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(900.0f, 640.0f), ImGuiCond_FirstUseEver);
    // Floating only, like the HUD (the dockspace takes no other windows).
    if (!ImGui::Begin("Statistics (L)###Statistics", &ui::g_game.stats_open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    DrawStatsContents(world);
    ImGui::End();
}
