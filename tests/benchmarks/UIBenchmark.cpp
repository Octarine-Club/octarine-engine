#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UILayoutBoxComponent.h"
#include "Components/UIRectComponent.h"
#include "ECS/Registry.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderQueue.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/UILayoutSystem.h"
#include "UI/OssParser.h"
#include "UI/OxmlParser.h"
#include "UI/UIRuntime.h"

namespace {

constexpr const char* kBenchOxmlDoc = R"(
<OXML>
  <Style src="ui/theme.oss" />
  <Use name="Card" src="ui/card.oxml" />
  <Param name="title" default="Inventory" />
  <Canvas id="hud" width="1280px" height="720px">
    <Column class="panel primary" width="100%" height="100%" padding="16 24" gap="8">
      <Row class="header" height="48px" justify="space-between" align="center">
        <Label ref="header" text="{title}" font="default-20" />
        <Label text="HP: {hp}/{maxHp}" font="default-20" />
      </Row>
      <ProgressBar ref="hpBar" value="{hp / maxHp}" width="320px" height="20px" />
      <Row gap="8" height="36px">
        <Button id="play-btn" class="primary" text="Play &amp; Win" on-click="onPlay" width="140px" height="36px" />
        <Button class="secondary" text="Options" width="140px" height="36px" />
        <Button class="secondary" text="Quit" width="140px" height="36px" />
      </Row>
      <Grid columns="4" gap="8" width="100%" height="1fr">
        <Panel each="item, idx in {items}" class="slot" width="64px" height="64px">
          <Label text="{item}" />
        </Panel>
      </Grid>
    </Column>
  </Canvas>
</OXML>
)";

constexpr const char* kBenchMountOxmlDoc = R"(
<OXML>
  <Param name="title" default="Inventory" />
  <Canvas id="hud" width="1280px" height="720px">
    <Column class="panel primary" width="100%" height="100%" padding="16 24" gap="8">
      <Row class="header" height="48px" justify="space-between" align="center">
        <Label ref="header" text="{title}" font="default-20" />
        <Label text="HP: {hp}/{maxHp}" font="default-20" />
      </Row>
      <ProgressBar ref="hpBar" value="{hp / maxHp}" width="320px" height="20px" />
      <Row gap="8" height="36px">
        <Button id="play-btn" class="primary" text="Play &amp; Win" on-click="onPlay" width="140px" height="36px" />
        <Button class="secondary" text="Options" width="140px" height="36px" />
        <Button class="secondary" text="Quit" width="140px" height="36px" />
      </Row>
      <Grid columns="4" gap="8" width="100%" height="1fr">
        <Panel each="item, idx in {items}" class="slot" width="64px" height="64px">
          <Label text="{item}" />
        </Panel>
      </Grid>
    </Column>
  </Canvas>
</OXML>
)";

constexpr const char* kBenchOssDoc = R"(
:root {
  --gold: #FFD166;
  --panel-bg: rgba(20, 16, 28, 0.9);
  --btn-h: 36px;
}
Button {
  height: var(--btn-h);
  background-color: #2A223E;
  border-color: #4A3E6D;
  border-width: 1px;
  color: #FFFFFF;
}
.primary {
  background-color: #3B2F5E;
}
.secondary {
  background-color: #241D38;
}
#play-btn {
  width: 160px;
}
Button:hover {
  background-color: #524382;
  border-color: var(--gold);
}
Button:active {
  background-color: #1F1833;
}
Button:checked {
  background-color: var(--gold);
}
)";

void PopulateFlexHierarchy(Registry& registry, int totalWidgets) {
  GameConfig cfg{};
  cfg.windowWidth = 1920;
  cfg.windowHeight = 1080;
  registry.Set<GameConfig>(cfg);
  // Each panel + item emits fill, track, and 4 border lines (up to 6 RenderKeys per widget).
  registry.Set<RenderQueue>(RenderQueue(static_cast<std::size_t>(totalWidgets) * 8 + 256));

  const Entity canvas = registry.CreateEntityWithBundle(
      UICanvasComponent(true, 1920.0f, 1080.0f, 5),
      UIFlexComponent(FlexDirection::Column, 8.0f, 16.0f, FlexAlign::Stretch, FlexAlign::Start));

  const int panels = std::max(1, totalWidgets / 16);
  const int itemsPerPanel = std::max(1, totalWidgets / panels);

  for (int p = 0; p < panels; ++p) {
    UILayoutBoxComponent panelBox;
    panelBox.SetWidthPercent(100.0f);
    panelBox.SetHeightFr(1.0f);
    UIFlexComponent panelFlex(FlexDirection::Row, 6.0f, 8.0f, FlexAlign::Center, FlexAlign::SpaceBetween);
    panelFlex.wrap = true;
    SquarePrimitiveComponent panelSq;
    panelSq.color = {28, 24, 38, 240};
    panelSq.borderThickness = 1.0f;
    panelSq.borderColor = {80, 70, 110, 255};

    const Entity panel =
        registry.CreateEntityWithBundle(panelBox, panelFlex, panelSq, UIClipComponent{}, UIRectComponent{});
    registry.SetParent(panel, canvas);

    for (int i = 0; i < itemsPerPanel; ++i) {
      UILayoutBoxComponent itemBox;
      if (i % 3 == 0) {
        itemBox.SetWidthFr(1.0f);
      } else if (i % 3 == 1) {
        itemBox.SetWidthPercent(20.0f);
      } else {
        itemBox.SetWidthPx(96.0f);
      }
      itemBox.SetHeightPx(32.0f);
      itemBox.SetMargin(2.0f);

      SquarePrimitiveComponent itemSq;
      itemSq.color = {55, 45, 80, 255};
      itemSq.trackColor = {20, 16, 30, 255};
      itemSq.fillRatio = 0.75f;
      itemSq.borderThickness = 1.0f;

      const Entity item = registry.CreateEntityWithBundle(itemBox, itemSq, UIRectComponent{});
      registry.SetParent(item, panel);
    }
  }
}

void BM_UILayout_FlexTree(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  Registry registry;
  PopulateFlexHierarchy(registry, n);
  UILayoutSystem layoutSys;
  layoutSys(ContextFacade(&registry, 0.016f, nullptr, nullptr));

  for (auto _ : state) {
    registry.Get<RenderQueue>().Clear();
    layoutSys(ContextFacade(&registry, 0.016f, nullptr, nullptr));
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n);
  state.counters["ns_per_widget"] = benchmark::Counter(static_cast<double>(state.iterations()) * static_cast<double>(n),
                                                       benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_UILayout_FlexTree)->Name("Sys/UI/LayoutFlexTree")->Arg(64)->Arg(256)->Arg(1024);

void BM_UIRenderPrimitive_Tree(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  Registry registry;
  PopulateFlexHierarchy(registry, n);
  UILayoutSystem layoutSys;
  layoutSys(ContextFacade(&registry, 0.016f, nullptr, nullptr));
  RenderUIPrimitiveSystem renderSys;

  for (auto _ : state) {
    auto& rq = registry.Get<RenderQueue>();
    rq.Clear();
    renderSys(ContextFacade(&registry, 0.016f, nullptr, nullptr));
    benchmark::DoNotOptimize(rq.Size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_UIRenderPrimitive_Tree)->Name("Sys/UI/RenderPrimitives")->Arg(64)->Arg(256)->Arg(1024);

void BM_UIOxml_Parse(benchmark::State& state) {
  for (auto _ : state) {
    auto res = OxmlParser::Parse(kBenchOxmlDoc);
    benchmark::DoNotOptimize(res.ok);
    benchmark::DoNotOptimize(res.document.roots.size());
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_UIOxml_Parse)->Name("Misc/UI/OxmlParse");

void BM_UIOss_ParseAndResolve(benchmark::State& state) {
  for (auto _ : state) {
    auto res = OssParser::Parse(kBenchOssDoc);
    auto style = res.stylesheet.Resolve("Button", {"primary"}, "play-btn", {{"margin", "4 8"}});
    auto patch = style.BuildStyleStateComponent();
    benchmark::DoNotOptimize(patch);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_UIOss_ParseAndResolve)->Name("Misc/UI/OssParseAndResolve");

void BM_UIRuntime_ReactiveRefresh(benchmark::State& state) {
  const int repeaterCount = static_cast<int>(state.range(0));
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 1280;
  cfg.windowHeight = 720;
  registry.Set<GameConfig>(cfg);

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

  UIRuntime uiRuntime(&lua, &registry, nullptr);
  sol::table uiTbl = lua.create_table();
  uiRuntime.InstallLuaFunctions(uiTbl);
  lua["ui"] = uiTbl;
  lua["bench_xml"] = kBenchMountOxmlDoc;
  lua["item_count"] = repeaterCount;

  lua.script(R"(
    local list = {}
    for i = 1, item_count do
      list[i] = "Item " .. tostring(i)
    end
    vm = ui.observable({
      title = "Backpack",
      hp = 85,
      maxHp = 100,
      items = list
    })
    view = ui.mount(bench_xml, { context = vm })
  )");

  int hpVal = 85;
  for (auto _ : state) {
    hpVal = (hpVal % 100) + 1;
    lua["vm"]["hp"] = hpVal;
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * (repeaterCount + 5));
}
BENCHMARK(BM_UIRuntime_ReactiveRefresh)->Name("Sys/UI/ReactiveRefresh")->Arg(8)->Arg(32);

}  // namespace
