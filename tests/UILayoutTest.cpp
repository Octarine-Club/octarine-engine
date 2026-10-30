#include <SDL3/SDL.h>

#include <iostream>

#include "Components/SpriteComponent.h"
#include "Components/SquarePrimitiveComponent.h"
#include "Components/TextLabelComponent.h"
#include "Components/UIAnchorComponent.h"
#include "Components/UICanvasComponent.h"
#include "Components/UIClipComponent.h"
#include "Components/UIFlexComponent.h"
#include "Components/UILayoutBoxComponent.h"
#include "Components/UINineSliceComponent.h"
#include "Components/UIRectComponent.h"
#include "Components/UIScrollComponent.h"
#include "Components/UIStyleStateComponent.h"
#include "Components/UIZIndexComponent.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"
#include "Events/KeyInputEvent.h"
#include "Events/MouseInputEvent.h"
#include "Events/MouseWheelEvent.h"
#include "Game/GameConfig.h"
#include "Renderer/RenderKey.h"
#include "Renderer/RenderQueue.h"
#include "Systems/RenderUIPrimitiveSystem.h"
#include "Systems/RenderUISpriteSystem.h"
#include "Systems/UIButtonSystem.h"
#include "Systems/UILayoutSystem.h"
#include "TestHarness.h"
#include "UI/OssParser.h"
#include "UI/OxmlParser.h"
#include "UI/UIRuntime.h"

using octarine::test::Check;
using octarine::test::CheckEq;

namespace {

void TestRenderKeySizeAndTextSortOrder() {
  std::cout << "[phase1] RenderKey 80-byte invariant and TEXT sort precedence\n";
  CheckEq(sizeof(RenderKey), static_cast<size_t>(80), "sizeof(RenderKey) remains 80 bytes with clipId");

  const std::uint64_t capKey = RenderKey::ComputeSortKey(1, 32.0f, CAPSULE_PRIMITIVE, nullptr);
  const std::uint64_t sqKey = RenderKey::ComputeSortKey(1, 32.0f, SQUARE_PRIMITIVE, nullptr);
  const std::uint64_t lineKey = RenderKey::ComputeSortKey(1, 32.0f, LINE_PRIMITIVE, nullptr);
  const std::uint64_t textKey = RenderKey::ComputeSortKey(1, 32.0f, TEXT, nullptr);
  Check(textKey > capKey, "TEXT sorts after CAPSULE_PRIMITIVE on same layer/depthBand (pill button fix)");
  Check(textKey > sqKey, "TEXT sorts after SQUARE_PRIMITIVE on same layer/depthBand");
  Check(textKey > lineKey, "TEXT sorts after LINE_PRIMITIVE on same layer/depthBand");
}

void TestUnitsMarginsAndSpaceBetween() {
  std::cout << "[phase1] UILayoutSystem resolves Px, Percent, Fr, margins, min/max, and SpaceBetween\n";
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 1000;
  cfg.windowHeight = 500;
  registry.Set<GameConfig>(cfg);
  registry.RegisterBulkSystem(UILayoutSystem());

  const Entity canvas = registry.CreateEntityWithBundle(UICanvasComponent(true, 1000.0f, 500.0f, 5));
  UIFlexComponent rowFlex(FlexDirection::Row, 0.0f, 0.0f, FlexAlign::Stretch, FlexAlign::Stretch);
  registry.AddComponent(canvas, rowFlex);

  // Child 1: 20% width (200px) with 10px left/right margin (total 220px)
  UILayoutBoxComponent box1;
  box1.SetWidthPercent(20.0f);
  box1.marginLeft = 10.0f;
  box1.marginRight = 10.0f;
  const Entity c1 = registry.CreateEntityWithBundle(box1);
  registry.SetParent(c1, canvas);

  // Child 2: 1fr
  UILayoutBoxComponent box2;
  box2.SetWidthFr(1.0f);
  const Entity c2 = registry.CreateEntityWithBundle(box2);
  registry.SetParent(c2, canvas);

  // Child 3: 3fr
  UILayoutBoxComponent box3;
  box3.SetWidthFr(3.0f);
  const Entity c3 = registry.CreateEntityWithBundle(box3);
  registry.SetParent(c3, canvas);

  registry.Update(0.016f);
  registry.ValidateInvariants();

  // Remaining space = 1000 - 220 = 780. 1fr = 195, 3fr = 585.
  const auto& r1 = registry.GetComponent<UIRectComponent>(c1);
  const auto& r2 = registry.GetComponent<UIRectComponent>(c2);
  const auto& r3 = registry.GetComponent<UIRectComponent>(c3);
  CheckEq(r1.left, 10.0f, "c1 left offset by marginLeft");
  CheckEq(r1.Width(), 200.0f, "c1 20% of 1000 = 200");
  CheckEq(r2.left, 220.0f, "c2 starts after c1 + marginRight");
  CheckEq(r2.Width(), 195.0f, "c2 1fr gets 1/4 of 780 = 195");
  CheckEq(r3.left, 415.0f, "c3 starts after c2");
  CheckEq(r3.Width(), 585.0f, "c3 3fr gets 3/4 of 780 = 585");

  // Test SpaceBetween when all children have fixed px widths
  box2.SetWidthPx(100.0f);
  box3.SetWidthPx(100.0f);
  box1.SetMargin(0.0f);
  box1.SetWidthPx(100.0f);
  registry.GetComponent<UILayoutBoxComponent>(c1) = box1;
  registry.GetComponent<UILayoutBoxComponent>(c2) = box2;
  registry.GetComponent<UILayoutBoxComponent>(c3) = box3;
  registry.GetComponent<UIFlexComponent>(canvas).justify = FlexAlign::SpaceBetween;

  registry.Update(0.016f);
  CheckEq(registry.GetComponent<UIRectComponent>(c1).left, 0.0f, "SpaceBetween first child at 0");
  CheckEq(registry.GetComponent<UIRectComponent>(c2).left, 450.0f, "SpaceBetween middle child centered at 450");
  CheckEq(registry.GetComponent<UIRectComponent>(c3).left, 900.0f, "SpaceBetween last child at 900");
}

void TestIntrinsicAutoSizingAndVisibility() {
  std::cout << "[phase1] Intrinsic Auto sizing (TextLabel + nested Row) and visible=false culling\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<RenderQueue>(RenderQueue(64));
  registry.RegisterBulkSystem(UILayoutSystem());
  registry.RegisterBulkSystem(RenderUIPrimitiveSystem());

  const Entity canvas = registry.CreateEntityWithBundle(UICanvasComponent(true, 800.0f, 600.0f, 1));
  UIFlexComponent rootFlex(FlexDirection::Row, 0.0f, 0.0f, FlexAlign::Start, FlexAlign::Start);
  registry.AddComponent(canvas, rootFlex);

  // Auto-sized badge container wrapping a 10-char text label ("0123456789" at font size 20 -> 10 * 11 = 110px + 20px
  // pad = 130px)
  UIFlexComponent badgeFlex(FlexDirection::Row, 0.0f, 10.0f, FlexAlign::Center, FlexAlign::Start);
  UILayoutBoxComponent badgeBox;
  badgeBox.widthUnit = UIUnit::Auto;
  badgeBox.heightUnit = UIUnit::Auto;
  const Entity badge = registry.CreateEntityWithBundle(badgeFlex, badgeBox, SquarePrimitiveComponent{});
  registry.SetParent(badge, canvas);

  TextLabelComponent lbl({0.0f, 0.0f}, 0, "0123456789", "default-20");
  UILayoutBoxComponent lblBox;
  lblBox.widthUnit = UIUnit::Auto;
  lblBox.heightUnit = UIUnit::Auto;
  const Entity lblEntity = registry.CreateEntityWithBundle(lbl, lblBox);
  registry.SetParent(lblEntity, badge);

  registry.Update(0.016f);
  const auto& badgeRect = registry.GetComponent<UIRectComponent>(badge);
  CheckEq(badgeRect.Width(), 130.0f, "Auto container width = text intrinsic width (110) + padding (20)");
  CheckEq(badgeRect.Height(), 40.0f, "Auto container height = text intrinsic height (20) + padding (20)");
  CheckEq(registry.Get<RenderQueue>().Size(), static_cast<size_t>(1), "Visible badge square emitted to RenderQueue");

  // Hide the badge and verify subtree culling
  registry.Get<RenderQueue>().Clear();
  registry.GetComponent<UILayoutBoxComponent>(badge).visible = false;
  registry.Update(0.016f);
  Check(!registry.GetComponent<UIRectComponent>(badge).visible, "badge rect marked invisible");
  Check(!registry.GetComponent<UIRectComponent>(lblEntity).visible, "child label rect marked invisible");
  CheckEq(registry.Get<RenderQueue>().Size(), static_cast<size_t>(0), "Hidden badge emits zero render commands");
}

void TestScissorClippingAndProgressBarTrack() {
  std::cout << "[phase1] UIClipComponent intersection + layer bump and SquarePrimitive trackColor\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<RenderQueue>(RenderQueue(64));
  registry.RegisterBulkSystem(UILayoutSystem());
  registry.RegisterBulkSystem(RenderUIPrimitiveSystem());

  const Entity canvas = registry.CreateEntityWithBundle(UICanvasComponent(true, 800.0f, 600.0f, 2));

  // Scroll viewport with UIClipComponent at (50, 50, 200x100)
  UILayoutBoxComponent clipBox;
  clipBox.SetWidthPx(200.0f);
  clipBox.SetHeightPx(100.0f);
  clipBox.marginLeft = 50.0f;
  clipBox.marginTop = 50.0f;
  const Entity viewport = registry.CreateEntityWithBundle(clipBox, UIClipComponent{});
  registry.SetParent(viewport, canvas);

  // Progress bar inside clipped viewport with 50% fillRatio + trackColor + border
  UILayoutBoxComponent barBox;
  barBox.SetWidthPx(160.0f);
  barBox.SetHeightPx(20.0f);
  SquarePrimitiveComponent barSq;
  barSq.color = {0, 255, 0, 255};
  barSq.trackColor = {40, 40, 40, 255};
  barSq.fillRatio = 0.5f;
  const Entity bar = registry.CreateEntityWithBundle(barBox, barSq);
  registry.SetParent(bar, viewport);

  registry.Update(0.016f);

  const auto& vpRect = registry.GetComponent<UIRectComponent>(viewport);
  const auto& barRect = registry.GetComponent<UIRectComponent>(bar);
  Check(vpRect.clipId != 0, "viewport registered non-zero clipId in RenderQueue");
  CheckEq(barRect.clipId, vpRect.clipId, "child inherits viewport clipId");
  CheckEq(barRect.layer, 3, "UIClipComponent bumps layer from 2 to 3 to prevent sibling interleaving");

  auto& rq = registry.Get<RenderQueue>();
  const SDL_Rect* clip = rq.GetClipRect(barRect.clipId);
  Check(clip != nullptr, "RenderQueue resolves clipId to SDL_Rect");
  if (clip) {
    CheckEq(clip->x, 50, "clip.x == 50");
    CheckEq(clip->y, 50, "clip.y == 50");
    CheckEq(clip->w, 200, "clip.w == 200");
    CheckEq(clip->h, 100, "clip.h == 100");
  }

  // Track rect (160w) + Fill rect (80w) = 2 commands, both carrying clipId
  CheckEq(rq.Size(), static_cast<size_t>(2), "ProgressBar emits track command + fill command");
  rq.Sort();
  auto it = rq.begin();
  CheckEq(it->clipId, barRect.clipId, "track command preserves clipId");
  CheckEq(it->payload.square.destRect.w, 160.0f, "track command spans full 160px width");
  ++it;
  CheckEq(it->clipId, barRect.clipId, "fill command preserves clipId");
  CheckEq(it->payload.square.destRect.w, 80.0f, "fill command spans 50% (80px) width");
}

void TestNineSliceEmission() {
  std::cout << "[phase1] RenderUISpriteSystem emits 9 sliced quads for UINineSliceComponent\n";
  RenderQueue rq(32);
  const UIRectComponent rect(10.0f, 20.0f, 110.0f, 70.0f, 4);  // 100x50
  SpriteComponent sprite("panel_bg", 32, 32, 0);
  const UINineSliceComponent nine(8.0f, 6.0f, 8.0f, 6.0f);
  const SDL_FRect atlasOffset{100.0f, 200.0f, 32.0f, 32.0f};

  RenderUISpriteSystem::EmitNineSlice(rq, rect, sprite, nine, nullptr, atlasOffset);
  CheckEq(rq.Size(), static_cast<size_t>(9), "9-slice sprite emits 9 SpriteCommands");

  auto it = rq.begin();
  // Top-left corner: dest (10, 20, 8x6), src (100, 200, 8x6)
  CheckEq(it->payload.sprite.destX, 10.0f, "TL destX");
  CheckEq(it->payload.sprite.destY, 20.0f, "TL destY");
  CheckEq(it->payload.sprite.destW, 8.0f, "TL destW");
  CheckEq(it->payload.sprite.destH, 6.0f, "TL destH");
  CheckEq(it->payload.sprite.srcRect.x, 100.0f, "TL srcX includes atlasOffset");
  CheckEq(it->payload.sprite.srcRect.y, 200.0f, "TL srcY includes atlasOffset");
  ++it;
  // Top-center edge: dest (18, 20, 84x6), src (108, 200, 16x6)
  CheckEq(it->payload.sprite.destX, 18.0f, "TC destX");
  CheckEq(it->payload.sprite.destW, 84.0f, "TC stretched width = 100 - 8 - 8");
  CheckEq(it->payload.sprite.srcRect.w, 16.0f, "TC srcW = 32 - 8 - 8");
}

void TestPhase2FocusZOrderClipAndStyleStates() {
  std::cout << "[phase2] Topmost Z-order hit stop, clip-rect hit culling, focus nav, scroll wheel, and UIStyleState\n";
  Registry registry;
  registry.Set<GameConfig>(GameConfig{});
  registry.Set<RenderQueue>(RenderQueue(64));
  auto eventBus = std::make_unique<EventBus>();
  auto& btnSys = registry.Set<UIButtonSystem>(UIButtonSystem());
  btnSys.Init(&registry, eventBus);
  registry.RegisterBulkSystem(UILayoutSystem());
  registry.RegisterBulkSystem(RenderUIPrimitiveSystem());

  const Entity canvas = registry.CreateEntityWithBundle(UICanvasComponent(true, 800.0f, 600.0f, 1));

  // Two overlapping buttons at (100, 100, 120x40): lower on layer 1, modal upper on layer 5
  int lowerClicks = 0;
  int upperClicks = 0;
  sol::state lua;
  lua.open_libraries(sol::lib::base);
  lua["on_lower"] = [&]() { ++lowerClicks; };
  lua["on_upper"] = [&]() { ++upperClicks; };

  UIButtonComponent lowerBtn;
  lowerBtn.clickFunction = lua["on_lower"];
  UIFocusComponent lowerFoc;
  lowerFoc.tabIndex = 1;
  const Entity lower = registry.CreateEntityWithBundle(UIRectComponent(100.0f, 100.0f, 220.0f, 140.0f, 1), lowerBtn,
                                                       lowerFoc, SquarePrimitiveComponent{});

  UIButtonComponent upperBtn;
  upperBtn.clickFunction = lua["on_upper"];
  UIFocusComponent upperFoc;
  upperFoc.tabIndex = 2;
  UIStyleStateComponent styleState;
  styleState.checked.SetBg({212, 175, 55, 255});
  styleState.hover.SetBorder({255, 255, 0, 255});
  SquarePrimitiveComponent upperSq;
  upperSq.color = {30, 30, 40, 255};
  upperSq.borderThickness = 2.0f;
  upperSq.borderColor = {100, 100, 100, 255};
  const Entity upper = registry.CreateEntityWithBundle(UIRectComponent(100.0f, 100.0f, 220.0f, 140.0f, 5), upperBtn,
                                                       upperFoc, styleState, upperSq);

  // Click at (150, 120): only upper button (layer 5) should fire, not lower (layer 1)
  SDL_MouseButtonEvent clickEv{};
  clickEv.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  clickEv.button = 1;
  clickEv.x = 150.0f;
  clickEv.y = 120.0f;
  eventBus->EmitEvent<MouseInputEvent>(clickEv);

  CheckEq(upperClicks, 1, "Topmost modal button (layer 5) receives click");
  CheckEq(lowerClicks, 0, "Overlapped lower button (layer 1) does not receive click-through");
  Check(registry.GetComponent<UIFocusComponent>(upper).isFocused, "Clicked button receives focus");

  // Tab navigation cycles focus to lower (tabIndex 1) then back to upper (tabIndex 2)
  eventBus->EmitEvent<KeyInputEvent>(SDLK_TAB, SDL_KMOD_NONE, true);
  Check(registry.GetComponent<UIFocusComponent>(lower).isFocused, "Tab moves focus to lower button");
  Check(!registry.GetComponent<UIFocusComponent>(upper).isFocused, "Upper button loses focus on Tab");

  // Pressing Enter triggers the currently focused button (lower)
  eventBus->EmitEvent<KeyInputEvent>(SDLK_RETURN, SDL_KMOD_NONE, true);
  CheckEq(lowerClicks, 1, "Pressing Return activates focused button");

  // Check :checked style state resolution on upper
  registry.GetComponent<UIFocusComponent>(upper).isChecked = true;
  registry.GetComponent<UIButtonComponent>(upper).isPressed = false;
  registry.GetComponent<UIButtonComponent>(upper).isHovered = false;
  const auto resolved =
      octarine::ui::ResolveVisualState(&registry, upper, upperSq.color, upperSq.borderColor, {255, 255, 255, 255});
  CheckEq(resolved.bg.r, static_cast<std::uint8_t>(212), ":checked patch overrides background color R");
  CheckEq(resolved.bg.g, static_cast<std::uint8_t>(175), ":checked patch overrides background color G");

  // ScrollView wheel test: viewport 200x100 with 3 children of 60px height each (= 180px contentHeight)
  UIFlexComponent scrollFlex(FlexDirection::Column, 0.0f, 0.0f, FlexAlign::Stretch, FlexAlign::Start);
  UILayoutBoxComponent scrollBox;
  scrollBox.SetWidthPx(200.0f);
  scrollBox.SetHeightPx(100.0f);
  scrollBox.marginLeft = 400.0f;
  scrollBox.marginTop = 100.0f;
  UIScrollComponent scrollComp;
  scrollComp.wheelStep = 25.0f;
  const Entity scrollView = registry.CreateEntityWithBundle(scrollFlex, scrollBox, scrollComp, UIClipComponent{});
  registry.SetParent(scrollView, canvas);

  for (int i = 0; i < 3; ++i) {
    UILayoutBoxComponent itemBox;
    itemBox.SetHeightPx(60.0f);
    const Entity item = registry.CreateEntityWithBundle(itemBox);
    registry.SetParent(item, scrollView);
  }
  registry.Update(0.016f);
  CheckEq(registry.GetComponent<UIScrollComponent>(scrollView).contentHeight, 180.0f,
          "ScrollView contentHeight measured as 3 * 60 = 180");

  // Hover inside ScrollView at (450, 150) and scroll wheel down (dy = -2.0 -> scrollY += 50)
  SDL_MouseButtonEvent moveEv{};
  moveEv.type = SDL_EVENT_MOUSE_MOTION;
  moveEv.x = 450.0f;
  moveEv.y = 150.0f;
  eventBus->EmitEvent<MouseInputEvent>(moveEv);
  eventBus->EmitEvent<MouseWheelEvent>(0.0f, -2.0f);
  CheckEq(registry.GetComponent<UIScrollComponent>(scrollView).scrollY, 50.0f, "Mouse wheel scrolls scrollY to 50");
}

void TestPhase3OxmlAndOssParsers() {
  std::cout << "[phase3] OxmlParser (.oxml) and OssParser (.oss) AST, variables, specificity, and pseudo-states\n";

  const char* oxmlSrc = R"(
    <OXML>
      <Style src="ui/theme.oss" />
      <Use name="Card" src="ui/card.oxml" />
      <Param name="title" default="Inventory" />
      <!-- Main HUD -->
      <Canvas id="hud">
        <Column class="panel primary" width="50%" padding="12 24">
          <Label ref="header" text="{title}" font="default-20" />
          <Button id="play-btn" text="Play &amp; Win" on-click="onPlay" />
          <Row each="item, idx in {items}" item-key="id">
            <Image sprite="sword_icon" />
          </Row>
        </Column>
      </Canvas>
    </OXML>
  )";

  const OxmlParseResult oxmlRes = OxmlParser::Parse(oxmlSrc);
  Check(oxmlRes.ok, "OxmlParser parses valid .oxml document");
  CheckEq(oxmlRes.document.styles.size(), static_cast<size_t>(1), "1 <Style> import extracted");
  CheckEq(oxmlRes.document.uses.size(), static_cast<size_t>(1), "1 <Use> import extracted");
  CheckEq(oxmlRes.document.params.size(), static_cast<size_t>(1), "1 <Param> declaration extracted");
  CheckEq(oxmlRes.document.roots.size(), static_cast<size_t>(1), "1 root <Canvas> node extracted");

  const OxmlNode& col = oxmlRes.document.roots[0].children[0];
  CheckEq(col.children[1].GetAttr("text"), std::string("Play & Win"), "XML entities decoded in attributes");

  const OxmlEachClause eachClause = OxmlParser::ParseEachClause(col.children[2].GetAttr("each"));
  Check(eachClause.valid, "ParseEachClause parses 'item, idx in {items}'");
  CheckEq(eachClause.itemVar, std::string("item"), "each itemVar == item");
  CheckEq(eachClause.indexVar, std::string("idx"), "each indexVar == idx");
  CheckEq(eachClause.listExpr, std::string("items"), "each listExpr == items");

  const OxmlAssetRefs refs = OxmlParser::CollectStaticAssetRefs(oxmlRes.document);
  CheckEq(refs.spriteIds.size(), static_cast<size_t>(1), "Static sprite ref 'sword_icon' collected for bake");
  CheckEq(refs.fontIds.size(), static_cast<size_t>(1), "Static font ref 'default-20' collected for bake");

  const char* ossSrc = R"(
    :root {
      --gold: #FFD166;
      --panel-bg: rgba(20, 16, 28, 0.9);
      --btn-h: 44px;
    }
    Button {
      height: var(--btn-h);
      background-color: #2A223E;
      color: #FFFFFF;
    }
    .primary {
      background-color: #3B2F5E;
    }
    #play-btn {
      width: 240px;
    }
    Button:hover {
      background-color: #524382;
      border-color: var(--gold);
    }
    Button:checked {
      background-color: var(--gold);
    }
  )";

  const OssParseResult ossRes = OssParser::Parse(ossSrc);
  Check(ossRes.ok, "OssParser parses valid .oss stylesheet");
  const OssResolvedStyle resolved = ossRes.stylesheet.Resolve("Button", {"primary"}, "play-btn", {{"margin", "8 16"}});
  CheckEq(resolved.Get("height"), std::string("44px"), "var(--btn-h) resolved from :root");
  CheckEq(resolved.Get("background-color"), std::string("#3B2F5E"), ".primary class overrides Button tag rule");
  CheckEq(resolved.Get("width"), std::string("240px"), "#play-btn ID rule applied");
  CheckEq(resolved.Get("margin"), std::string("8 16"), "Inline style merged");
  Check(resolved.HasPseudoStates(), "Pseudo-states (:hover, :checked) detected");

  const UIStyleStateComponent stateComp = resolved.BuildStyleStateComponent();
  Check(stateComp.hover.HasBg(), ":hover background patch populated");
  Check(stateComp.hover.HasBorder(), ":hover border patch populated with var(--gold)");
  CheckEq(stateComp.hover.borderColor.r, static_cast<uint8_t>(0xFF), ":hover border R == 0xFF");
  CheckEq(stateComp.hover.borderColor.g, static_cast<uint8_t>(0xD1), ":hover border G == 0xD1");
  Check(stateComp.checked.HasBg(), ":checked background patch populated");
}

void TestPhase4UnifiedSpineMountBindingsAndWidgets() {
  std::cout << "[phase4] Unified Spine: ui.mount, ui.observable, refs, if/each, RadioGroup, Toggle, Slider, and custom "
               "widgets\n";
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 1280;
  cfg.windowHeight = 720;
  registry.Set<GameConfig>(cfg);
  registry.RegisterBulkSystem(UILayoutSystem());

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

  UIRuntime uiRuntime(&lua, &registry, nullptr);
  sol::table uiTbl = lua.create_table();
  uiRuntime.InstallLuaFunctions(uiTbl);
  lua["ui"] = uiTbl;

  // Register a custom reusable primitive widget in Lua
  lua.script(R"(
    ui.register_widget("StatBadge", {
      props = { label = "STAT", val = "0" },
      template = [[
        <Row width="120px" height="28px">
          <Label text="{label}: {val}" />
        </Row>
      ]],
      on_mount = function(ctx)
        _G.badge_mounted = true
      end,
      on_prop_changed = function(ctx, k, v)
        _G.last_badge_prop = k .. "=" .. tostring(v)
      end,
      on_unmount = function(ctx)
        _G.badge_unmounted = true
      end
    })
  )");

  const char* viewXml = R"(
    <OXML>
      <Canvas width="800px" height="600px">
        <Column width="100%" height="100%" padding="16" gap="8">
          <Label ref="hpLabel" text="HP: {hp}/{maxHp}" />
          <ProgressBar ref="hpBar" value="{hp / maxHp}" width="200px" height="20px" />
          <Label ref="lowHpBanner" if="{hp < 30}" text="LOW HEALTH!" />
          <Toggle ref="muteToggle" text="Mute" bind-value="muted" on-change="onMuteChange" width="120px" height="32px" />
          <RadioGroup bind-value="difficulty" on-change="onDiffChange" direction="row" gap="8">
            <RadioButton ref="rbEasy" value="easy" text="Easy" width="80px" height="32px" />
            <RadioButton ref="rbHard" value="hard" text="Hard" width="80px" height="32px" />
          </RadioGroup>
          <Slider ref="volSlider" min="0" max="100" step="5" bind-value="volume" on-change="onVolChange" width="200px" height="24px" />
          <StatBadge label="ATK" val="{atk}" />
          <Column ref="invList" width="200px" height="auto">
            <Label each="item in {items}" text="{item}" height="20px" />
          </Column>
          <Button ref="healBtn" text="Heal" on-click="onHeal" width="100px" height="36px" />
        </Column>
      </Canvas>
    </OXML>
  )";

  lua["view_xml"] = viewXml;
  lua.script(R"(
    vm = ui.observable({
      hp = 80,
      maxHp = 100,
      atk = 15,
      muted = false,
      difficulty = "easy",
      volume = 50,
      items = { "Sword", "Shield" }
    })

    controller = {
      heal_clicks = 0,
      last_diff = "",
      last_mute = false,
      onHeal = function(self)
        self.heal_clicks = self.heal_clicks + 1
        vm.hp = 100
      end,
      onDiffChange = function(self, val)
        self.last_diff = val
      end,
      onMuteChange = function(self, val)
        self.last_mute = val
      end
    }

    view = ui.mount(view_xml, {
      context = vm,
      controller = controller
    })
  )");

  registry.Update(0.016f);
  registry.ValidateInvariants();

  sol::table view = lua.get<sol::table>("view");
  sol::table refs = view.get<sol::table>("refs");
  auto refEnt = [&](const char* id) -> Entity { return refs.get<sol::table>(id).get<Entity>("entity"); };
  auto callRefStr = [&](const char* id, const char* fn) -> std::string {
    sol::table h = refs.get<sol::table>(id);
    return h.get<sol::protected_function>(fn)(h).get<std::string>();
  };
  auto callRefFloat = [&](const char* id, const char* fn) -> float {
    sol::table h = refs.get<sol::table>(id);
    return h.get<sol::protected_function>(fn)(h).get<float>();
  };
  auto callRefBool = [&](const char* id, const char* fn) -> bool {
    sol::table h = refs.get<sol::table>(id);
    return h.get<sol::protected_function>(fn)(h).get<bool>();
  };

  Check(lua.get_or("badge_mounted", false), "Custom widget StatBadge on_mount fired");

  // Verify initial bound values
  CheckEq(callRefStr("hpLabel", "get_text"), std::string("HP: 80/100"), "Initial interpolated text binding");
  Check(std::abs(callRefFloat("hpBar", "get_value") - 0.8f) < 0.001f, "Initial ProgressBar value {hp / maxHp} == 0.8");
  Check(!callRefBool("lowHpBanner", "is_visible"), "if='{hp < 30}' initially hidden when hp=80");

  // Verify initial RadioGroup state ("easy" checked, "hard" unchecked)
  Check(callRefBool("rbEasy", "is_checked"), "rbEasy initially checked from bind-value='difficulty'");
  Check(!callRefBool("rbHard", "is_checked"), "rbHard initially unchecked");

  // Verify repeater spawned 2 children under invList
  const Entity invListEnt = refEnt("invList");
  CheckEq(registry.GetChildren(invListEnt).size(), static_cast<size_t>(2), "each='item in {items}' spawned 2 labels");

  // Mutate observable VM: hp = 20, atk = 25, items = 3 items
  lua.script(R"(
    vm.hp = 20
    vm.atk = 25
    vm.items = { "Sword", "Shield", "Potion" }
  )");

  CheckEq(callRefStr("hpLabel", "get_text"), std::string("HP: 20/100"),
          "Observable mutation updates hpLabel text automatically");
  Check(std::abs(callRefFloat("hpBar", "get_value") - 0.2f) < 0.001f,
        "Observable mutation updates ProgressBar fillRatio to 0.2");
  Check(callRefBool("lowHpBanner", "is_visible"), "Observable mutation reveals lowHpBanner when hp < 30");
  CheckEq(lua.get_or("last_badge_prop", std::string("")), std::string("val=25"),
          "Custom widget on_prop_changed fired when bound prop changed");
  CheckEq(registry.GetChildren(invListEnt).size(), static_cast<size_t>(3),
          "Observable list update reconciles repeater to 3 children");

  // Simulate clicking the "Hard" RadioButton: should uncheck Easy, check Hard, and update vm.difficulty
  const Entity rbHardEnt = refEnt("rbHard");
  registry.GetComponent<UIButtonComponent>(rbHardEnt).clickFunction();
  Check(!callRefBool("rbEasy", "is_checked"), "Clicking Hard unchecks Easy radio button");
  Check(callRefBool("rbHard", "is_checked"), "Clicking Hard checks Hard radio button");
  CheckEq(lua.get<sol::table>("vm").get<std::string>("difficulty"), std::string("hard"),
          "RadioGroup two-way bind-value updates vm.difficulty to 'hard'");
  CheckEq(lua.get<sol::table>("controller").get<std::string>("last_diff"), std::string("hard"),
          "RadioGroup on-change controller callback received 'hard'");

  // Simulate clicking the Mute Toggle: should flip vm.muted to true
  const Entity muteEnt = refEnt("muteToggle");
  registry.GetComponent<UIButtonComponent>(muteEnt).clickFunction();
  Check(callRefBool("muteToggle", "is_checked"), "Toggle isChecked becomes true on click");
  Check(lua.get<sol::table>("vm").get<bool>("muted"), "Toggle two-way bind-value updates vm.muted to true");

  // Simulate clicking Heal button -> controller.onHeal sets vm.hp = 100
  const Entity healEnt = refEnt("healBtn");
  registry.GetComponent<UIButtonComponent>(healEnt).clickFunction();
  CheckEq(callRefStr("hpLabel", "get_text"), std::string("HP: 100/100"),
          "Button on-click controller callback mutated vm.hp -> updated label");

  // Unmount view and verify cleanup
  lua.script("view:unmount()");
  Check(lua.get_or("badge_unmounted", false), "Custom widget on_unmount fired on view:unmount()");
  CheckEq(registry.GetUserEntityCount(), static_cast<uint64_t>(0), "view:unmount() despawns all UI entities cleanly");
  registry.ValidateInvariants();
}

void TestFieldReportRegressionsAndWrapping() {
  std::cout << "[phase5] Field report regressions: non-UI children, <Use> if/ref/reactive params, dynamic class/style, "
               "and Label wrapping\n";
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 800;
  cfg.windowHeight = 600;
  registry.Set<GameConfig>(cfg);
  registry.RegisterBulkSystem(UILayoutSystem());

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

  UIRuntime uiRuntime(&lua, &registry, nullptr);
  sol::table uiTbl = lua.create_table();
  uiRuntime.InstallLuaFunctions(uiTbl);
  lua["ui"] = uiTbl;

  // 1. Register a custom widget to test if/visible/ref on custom widget tags
  lua.script(R"(
    ui.register_widget("PillBadge", {
      props = { badge = "" },
      template = [[
        <Row width="80px" height="24px">
          <Label text="{badge}" />
        </Row>
      ]]
    })
  )");

  const char* xml = R"(
    <OXML>
      <Use name="ScreenHeader" src="&lt;OXML&gt;&lt;Param name='right_top' default='0' /&gt;&lt;Row width='200px' height='24px'&gt;&lt;Label text='{right_top}' /&gt;&lt;/Row&gt;&lt;/OXML&gt;" />
      <Canvas ref="rootCanvas" width="800px" height="600px" justify="center" align="center">
        <Column ref="card" width="240px" height="auto" padding="10" gap="8"
                background-color="{card_bg}" border-color="{card_border}" border-width="2">
          <ScreenHeader ref="hdrRef" if="{show_hdr}" right_top="{bone_text}" />
          <Button ref="tabBtn" class="tab-btn {tab_class}" text="Tab" width="100px" height="30px" />
          <Label ref="statusLbl" text="Status" text-color="{status_color}" width="{status_w}" height="20px" />
          <PillBadge ref="pillRef" if="{badge ~= ''}" badge="{badge}" />
          <Label ref="wrappedLbl" wrap="true" font="default-20"
                 text="Alpha Beta Gamma Delta Epsilon Zeta Eta Theta" />
        </Column>
      </Canvas>
    </OXML>
  )";

  lua["test_xml"] = xml;
  lua.script(R"(
    state = ui.observable({
      card_bg = "#102030",
      card_border = "#304050",
      tab_class = "",
      status_color = "#00FF00",
      status_w = "120px",
      badge = "",
      show_hdr = true,
      bone_text = "10 BONES"
    })
    fview = ui.mount(test_xml, {
      context = state,
      styles = {
        [[
          .tab-btn {
            background-color: #202030;
            border-color: #404050;
            color: #AAAAAA;
          }
          .tab-btn.active {
            background-color: #FFD166;
            border-color: #FFFFFF;
            color: #1A162B;
          }
        ]]
      }
    })
  )");

  // Attach a non-UI helper/controller entity (no UI components) to fview.root
  sol::table fview = lua.get<sol::table>("fview");
  const Entity rootEnt = fview.get<Entity>("root");
  const Entity helperScriptEnt = registry.CreateEntity();
  registry.SetParent(helperScriptEnt, rootEnt);

  registry.Update(0.016f);
  registry.ValidateInvariants();

  sol::table refs = fview.get<sol::table>("refs");
  auto refEnt = [&](const char* id) -> Entity { return refs.get<sol::table>(id).get<Entity>("entity"); };
  auto callRefBool = [&](const char* id, const char* fn) -> bool {
    sol::table h = refs.get<sol::table>(id);
    return h.get<sol::protected_function>(fn)(h).get<bool>();
  };

  // Bug 1 check: helperScriptEnt must not steal 1fr or break justify="center" on rootCanvas
  Check(!registry.HasComponent<UIRectComponent>(helperScriptEnt),
        "Non-UI entity parented to Canvas does not receive UIRectComponent");
  const Entity cardEnt = refEnt("card");
  const auto& cardRect = registry.GetComponent<UIRectComponent>(cardEnt);
  Check(cardRect.top > 100.0f, "Card remains vertically centered on Canvas despite non-UI child entity");

  // Bug 2 check: ref and if="{badge ~= ''}" on custom widget <PillBadge>
  const sol::object pillRefObj = refs["pillRef"];
  Check(pillRefObj.valid() && pillRefObj != sol::lua_nil, "ref='pillRef' registered on custom widget tag");
  if (pillRefObj.valid() && pillRefObj != sol::lua_nil) {
    Check(!callRefBool("pillRef", "is_visible"), "PillBadge hidden when badge == ''");
  }

  // Bug 4 check: initial dynamic class & inline style bindings, then reactive update outside each
  const Entity tabBtnEnt = refEnt("tabBtn");
  const Entity statusLblEnt = refEnt("statusLbl");
  CheckEq(registry.GetComponent<SquarePrimitiveComponent>(tabBtnEnt).color.r, static_cast<uint8_t>(0x20),
          "tabBtn initial background R from .tab-btn");
  CheckEq(registry.GetComponent<TextLabelComponent>(statusLblEnt).color.g, static_cast<uint8_t>(0xFF),
          "statusLbl initial text-color G == 0xFF");
  CheckEq(registry.GetComponent<UIRectComponent>(statusLblEnt).Width(), 120.0f, "statusLbl initial width == 120px");

  // Bug 5 check: wrappedLbl inside 240px card (20px horizontal padding -> 220px content width)
  // "Alpha Beta Gamma Delta Epsilon Zeta Eta Theta" is 45 chars; at default-20 (11px/char = 495px unwrapped),
  // wrapping at 220px produces multiple lines (> 20px height).
  const Entity wrappedLblEnt = refEnt("wrappedLbl");
  const auto& wrappedRect = registry.GetComponent<UIRectComponent>(wrappedLblEnt);
  Check(wrappedRect.Height() >= 40.0f, "Wrapped Label measures multi-line height (>= 40px)");

  // Mutate observable state to verify reactivity for Bug 2, Bug 3, and Bug 4
  lua.script(R"(
    state.tab_class = "active"
    state.card_bg = "#AA1122"
    state.card_border = "#BB3344"
    state.status_color = "#FF0055"
    state.status_w = "180px"
    state.badge = "NEW"
    state.bone_text = "99 BONES"
    state.show_hdr = false
  )");
  registry.Update(0.016f);

  const sol::object hdrRefObj = refs["hdrRef"];
  Check(hdrRefObj.valid() && hdrRefObj != sol::lua_nil, "ref='hdrRef' registered on <Use> tag");
  if (hdrRefObj.valid() && hdrRefObj != sol::lua_nil) {
    Check(!callRefBool("hdrRef", "is_visible"), "<Use> ScreenHeader hidden when state.show_hdr mutates to false");
    const Entity hdrEnt = refEnt("hdrRef");
    const auto hdrChildren = registry.GetChildren(hdrEnt);
    if (!hdrChildren.empty()) {
      CheckEq(registry.GetComponent<TextLabelComponent>(hdrChildren[0]).text, std::string("99 BONES"),
              "<Use> ScreenHeader <Param name='right_top'> updated reactively to '99 BONES'");
    }
  }

  if (pillRefObj.valid() && pillRefObj != sol::lua_nil) {
    Check(callRefBool("pillRef", "is_visible"), "PillBadge becomes visible when state.badge mutates to 'NEW'");
    const Entity pillEnt = refEnt("pillRef");
    const auto pillChildren = registry.GetChildren(pillEnt);
    if (!pillChildren.empty()) {
      CheckEq(registry.GetComponent<TextLabelComponent>(pillChildren[0]).text, std::string("NEW"),
              "PillBadge internal template label updated reactively to 'NEW'");
    }
  }
  CheckEq(registry.GetComponent<SquarePrimitiveComponent>(tabBtnEnt).color.r, static_cast<uint8_t>(0xFF),
          "tabBtn reactive class='tab-btn {tab_class}' updates background R to 0xFF (.active)");
  CheckEq(registry.GetComponent<SquarePrimitiveComponent>(cardEnt).color.r, static_cast<uint8_t>(0xAA),
          "card reactive background-color='{card_bg}' updates R to 0xAA");
  CheckEq(registry.GetComponent<SquarePrimitiveComponent>(cardEnt).borderColor.r, static_cast<uint8_t>(0xBB),
          "card reactive border-color='{card_border}' updates R to 0xBB");
  CheckEq(registry.GetComponent<TextLabelComponent>(statusLblEnt).color.r, static_cast<uint8_t>(0xFF),
          "statusLbl reactive text-color='{status_color}' updates R to 0xFF");
  CheckEq(registry.GetComponent<TextLabelComponent>(statusLblEnt).color.g, static_cast<uint8_t>(0x00),
          "statusLbl reactive text-color='{status_color}' updates G to 0x00");
  CheckEq(registry.GetComponent<UIRectComponent>(statusLblEnt).Width(), 180.0f,
          "statusLbl reactive width='{status_w}' updates width to 180px");

  lua.script("fview:unmount()");
}

struct AudioListener {
  std::vector<std::string> clips{};
  void OnAudioPlay(const AudioPlayEvent& ev) { clips.push_back(ev.clipId); }
};

void TestQ1DebugWireframeAndAudioHooks() {
  std::cout << "[q1] Slice 1 (ui.debug_draw wireframe overlay) and Slice 2 (sound-hover/click/focus audio hooks)\n";
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 800;
  cfg.windowHeight = 600;
  registry.Set<GameConfig>(cfg);
  registry.Set<RenderQueue>(RenderQueue(128));
  auto eventBus = std::make_unique<EventBus>();
  auto& btnSys = registry.Set<UIButtonSystem>(UIButtonSystem());
  btnSys.Init(&registry, eventBus);
  registry.RegisterBulkSystem(UILayoutSystem());
  registry.RegisterBulkSystem(RenderUIPrimitiveSystem());

  AudioListener audioListener;
  auto sub = eventBus->SubscribeEvent<AudioListener, AudioPlayEvent>(&audioListener, &AudioListener::OnAudioPlay);

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
  UIRuntime uiRuntime(&lua, &registry, nullptr);
  sol::table uiTbl = lua.create_table();
  uiRuntime.InstallLuaFunctions(uiTbl);
  lua["ui"] = uiTbl;

  const char* xml = R"(
    <OXML>
      <Canvas width="800px" height="600px" base-layer="2">
        <Row width="300px" height="100px" gap="10">
          <Button ref="btn1" text="One" width="120px" height="40px" sound-focus="sfx_focus_attr" />
          <Button ref="btn2" text="Two" width="120px" height="40px" />
        </Row>
      </Canvas>
    </OXML>
  )";

  lua["q1_xml"] = xml;
  lua.script(R"(
    q1_view = ui.mount(q1_xml, {
      styles = {
        [[
          Button {
            sound-hover: "sfx_hover_oss";
            sound-click: "sfx_click_oss";
          }
          Button:focus {
            sound: "sfx_focus_oss";
          }
        ]]
      }
    })
  )");

  // Verify static audio ref collection for -m bake
  const OxmlParseResult parsed = OxmlParser::Parse(xml);
  const OxmlAssetRefs refs = OxmlParser::CollectStaticAssetRefs(parsed.document);
  CheckEq(refs.audioIds.size(), static_cast<size_t>(1), "CollectStaticAssetRefs captures static sound-* attribute");
  CheckEq(refs.audioIds[0], std::string("sfx_focus_attr"), "Captured audio ref matches sfx_focus_attr");

  // Run layout + primitive render with debug_draw disabled first
  registry.Get<RenderQueue>().Clear();
  registry.Update(0.016f);
  const size_t normalCmdCount = registry.Get<RenderQueue>().Size();
  Check(normalCmdCount > 0, "Normal UI pass emits button primitives");

  // Enable ui.debug_draw(true) and verify wireframe borders emitted at layer >= 1000
  lua.script("dbg_on = ui.debug_draw(true)");
  Check(lua.get<bool>("dbg_on"), "ui.debug_draw(true) returns true");
  registry.Get<RenderQueue>().Clear();
  registry.Update(0.016f);
  const size_t debugCmdCount = registry.Get<RenderQueue>().Size();
  Check(debugCmdCount > normalCmdCount, "ui.debug_draw(true) emits additional wireframe border commands");

  // Toggle ui.debug_draw() with no args -> false
  lua.script("dbg_off = ui.debug_draw()");
  Check(!lua.get<bool>("dbg_off"), "ui.debug_draw() toggles back to false");

  // Test Slice 2 audio events: hover enter on btn1 (at 60, 20)
  SDL_MouseButtonEvent moveEv{};
  moveEv.type = SDL_EVENT_MOUSE_MOTION;
  moveEv.x = 60.0f;
  moveEv.y = 20.0f;
  eventBus->EmitEvent<MouseInputEvent>(moveEv);
  CheckEq(audioListener.clips.size(), static_cast<size_t>(1), "Hover enter emits 1 AudioPlayEvent");
  if (!audioListener.clips.empty()) {
    CheckEq(audioListener.clips.back(), std::string("sfx_hover_oss"), "Hover sound resolved from .oss Button rule");
  }

  // Moving within the same button must NOT re-trigger hover sound
  moveEv.x = 65.0f;
  eventBus->EmitEvent<MouseInputEvent>(moveEv);
  CheckEq(audioListener.clips.size(), static_cast<size_t>(1), "Staying hovered does not re-emit hover sound");

  // Clicking btn1 sets focus (sfx_focus_attr) and triggers click (sfx_click_oss)
  SDL_MouseButtonEvent clickEv{};
  clickEv.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  clickEv.button = 1;
  clickEv.x = 65.0f;
  clickEv.y = 20.0f;
  eventBus->EmitEvent<MouseInputEvent>(clickEv);
  CheckEq(audioListener.clips.size(), static_cast<size_t>(3), "Click emits focus sound + click sound");
  if (audioListener.clips.size() >= 3) {
    CheckEq(audioListener.clips[1], std::string("sfx_focus_attr"), "Focus sound resolved from inline sound-focus attr");
    CheckEq(audioListener.clips[2], std::string("sfx_click_oss"), "Click sound resolved from .oss sound-click");
  }

  // Tab to btn2 -> emits sfx_focus_oss from Button:focus pseudo-rule
  eventBus->EmitEvent<KeyInputEvent>(SDLK_TAB, SDL_KMOD_NONE, true);
  CheckEq(audioListener.clips.size(), static_cast<size_t>(4), "Tab focus change to btn2 emits focus sound");
  if (audioListener.clips.size() >= 4) {
    CheckEq(audioListener.clips[3], std::string("sfx_focus_oss"), "btn2 focus sound resolved from Button:focus rule");
  }

  lua.script("q1_view:unmount()");
}

void TestQ1GridAndFlexWrapAndSlots() {
  std::cout << "[q1] Slice 3 (<Grid columns> & flex-wrap) and Slice 4 (named <Slot> + caller scope in each)\n";
  Registry registry;
  GameConfig cfg{};
  cfg.windowWidth = 800;
  cfg.windowHeight = 600;
  registry.Set<GameConfig>(cfg);
  registry.RegisterBulkSystem(UILayoutSystem());

  sol::state lua;
  lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
  UIRuntime uiRuntime(&lua, &registry, nullptr);
  sol::table uiTbl = lua.create_table();
  uiRuntime.InstallLuaFunctions(uiTbl);
  lua["ui"] = uiTbl;

  const char* xml = R"(
    <OXML>
      <Use name="ItemSlotCard" src="&lt;OXML&gt;&lt;Param name='frame_title' default='Slot' /&gt;&lt;Column width='100%' height='50px'&gt;&lt;Slot name='header'&gt;&lt;Label text='{frame_title}' height='16px' /&gt;&lt;/Slot&gt;&lt;Slot /&gt;&lt;/Column&gt;&lt;/OXML&gt;" />
      <Canvas width="800px" height="600px">
        <Column width="400px" height="auto" gap="12">
          <Grid ref="invGrid" columns="3" gap="10" cross-gap="8" width="320px" height="auto">
            <ItemSlotCard each="item, idx in {items}">
              <Label text="{item.name} #{idx}" height="20px" />
            </ItemSlotCard>
          </Grid>
          <Row ref="tagCloud" wrap="true" gap="6" cross-gap="4" width="180px" height="auto">
            <Box width="80px" height="24px" />
            <Box width="80px" height="24px" />
            <Box width="80px" height="24px" />
          </Row>
          <ItemSlotCard ref="customHeaderCard" frame_title="IgnoredDefault">
            <Label slot="header" ref="customHdrLbl" text="Custom Header!" height="16px" />
            <Label ref="customBodyLbl" text="Custom Body!" height="20px" />
          </ItemSlotCard>
        </Column>
      </Canvas>
    </OXML>
  )";

  lua["grid_xml"] = xml;
  lua.script(R"(
    gstate = ui.observable({
      items = {
        { name = "Sword" },
        { name = "Shield" },
        { name = "Potion" },
        { name = "Bow" },
        { name = "Ring" },
        { name = "Helm" }
      }
    })
    gview = ui.mount(grid_xml, { context = gstate })
  )");

  registry.Update(0.016f);
  registry.ValidateInvariants();

  sol::table gview = lua.get<sol::table>("gview");
  sol::table refs = gview.get<sol::table>("refs");
  auto refEnt = [&](const char* id) -> Entity { return refs.get<sol::table>(id).get<Entity>("entity"); };

  // 1. Verify <Grid columns="3" gap="10" cross-gap="8" width="320px"> with 6 items (50px tall each)
  // Cell width = (320 - 2*10) / 3 = 100px.
  // Row 0 at top=0..50, Row 1 at top=58..108. Auto height of invGrid = 50 + 8 + 50 = 108px.
  const Entity gridEnt = refEnt("invGrid");
  const auto& gridRect = registry.GetComponent<UIRectComponent>(gridEnt);
  CheckEq(gridRect.Width(), 320.0f, "Grid width == 320px");
  CheckEq(gridRect.Height(), 108.0f, "Grid auto height == 2 rows * 50px + 8px cross-gap = 108px");

  const auto gridChildren = registry.GetChildren(gridEnt);
  CheckEq(gridChildren.size(), static_cast<size_t>(6), "Grid spawned 6 ItemSlotCard children");
  if (gridChildren.size() == 6) {
    const auto& c0 = registry.GetComponent<UIRectComponent>(gridChildren[0]);
    const auto& c1 = registry.GetComponent<UIRectComponent>(gridChildren[1]);
    const auto& c2 = registry.GetComponent<UIRectComponent>(gridChildren[2]);
    const auto& c3 = registry.GetComponent<UIRectComponent>(gridChildren[3]);
    CheckEq(c0.left, 0.0f, "Grid item 0 col 0 left == 0");
    CheckEq(c0.Width(), 100.0f, "Grid item 0 width == 100px");
    CheckEq(c1.left, 110.0f, "Grid item 1 col 1 left == 110");
    CheckEq(c2.left, 220.0f, "Grid item 2 col 2 left == 220");
    CheckEq(c3.left, 0.0f, "Grid item 3 wraps to col 0 on row 1");
    CheckEq(c3.top, 58.0f, "Grid item 3 row 1 top == 50 + 8 = 58");

    // Verify Slice 4: inside ItemSlotCard, child[0] is default fallback <Slot name='header'> ("Slot")
    // and child[1] is projected <Slot /> body evaluated against caller repeater scope ("{item.name} #{idx}")
    const auto card0Kids = registry.GetChildren(gridChildren[0]);
    CheckEq(card0Kids.size(), static_cast<size_t>(2), "ItemSlotCard has fallback header + projected body child");
    if (card0Kids.size() == 2) {
      CheckEq(registry.GetComponent<TextLabelComponent>(card0Kids[0]).text, std::string("Slot"),
              "Unfilled named <Slot name='header'> renders its default fallback child");
      CheckEq(registry.GetComponent<TextLabelComponent>(card0Kids[1]).text, std::string("Sword #1"),
              "Projected <Slot /> child resolves caller repeater scope variables {item.name} and {idx}");
    }
    const auto card5Kids = registry.GetChildren(gridChildren[5]);
    if (card5Kids.size() == 2) {
      CheckEq(registry.GetComponent<TextLabelComponent>(card5Kids[1]).text, std::string("Helm #6"),
              "6th projected <Slot /> child resolves 'Helm #6'");
    }
  }

  // 2. Verify <Row wrap="true" gap="6" cross-gap="4" width="180px"> with three 80px boxes:
  // Line 0 fits 2 boxes (80 + 6 + 80 = 166 <= 180), 3rd box wraps to Line 1 (top = 24 + 4 = 28).
  const Entity cloudEnt = refEnt("tagCloud");
  const auto& cloudRect = registry.GetComponent<UIRectComponent>(cloudEnt);
  CheckEq(cloudRect.Height(), 52.0f, "Wrapped Row auto height == 24 + 4 + 24 = 52px");
  const auto cloudKids = registry.GetChildren(cloudEnt);
  if (cloudKids.size() == 3) {
    const float cloudTop = cloudRect.top;
    CheckEq(registry.GetComponent<UIRectComponent>(cloudKids[0]).top - cloudTop, 0.0f, "Box 0 on line 0");
    CheckEq(registry.GetComponent<UIRectComponent>(cloudKids[1]).left, 86.0f, "Box 1 at 80 + 6 = 86");
    CheckEq(registry.GetComponent<UIRectComponent>(cloudKids[2]).left, 0.0f, "Box 2 wraps to left = 0");
    CheckEq(registry.GetComponent<UIRectComponent>(cloudKids[2]).top - cloudTop, 28.0f, "Box 2 wraps to top = 28");
  }

  // 3. Verify named slot override on customHeaderCard (<Label slot="header">)
  const Entity customCardEnt = refEnt("customHeaderCard");
  const auto customCardKids = registry.GetChildren(customCardEnt);
  CheckEq(customCardKids.size(), static_cast<size_t>(2), "customHeaderCard has 2 children (named header + default)");
  if (customCardKids.size() == 2) {
    CheckEq(registry.GetComponent<TextLabelComponent>(customCardKids[0]).text, std::string("Custom Header!"),
            "slot='header' child overrides <Slot name='header'> fallback");
    CheckEq(registry.GetComponent<TextLabelComponent>(customCardKids[1]).text, std::string("Custom Body!"),
            "Unnamed child projects into default <Slot />");
  }

  lua.script("gview:unmount()");
}

}  // namespace

int main() {
  TestRenderKeySizeAndTextSortOrder();
  TestUnitsMarginsAndSpaceBetween();
  TestIntrinsicAutoSizingAndVisibility();
  TestScissorClippingAndProgressBarTrack();
  TestNineSliceEmission();
  TestPhase2FocusZOrderClipAndStyleStates();
  TestPhase3OxmlAndOssParsers();
  TestPhase4UnifiedSpineMountBindingsAndWidgets();
  TestFieldReportRegressionsAndWrapping();
  TestQ1DebugWireframeAndAudioHooks();
  TestQ1GridAndFlexWrapAndSlots();
  return octarine::test::ReportSummary("UILayoutTest");
}
