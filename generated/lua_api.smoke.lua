-- AUTO-GENERATED Octarine Lua API surface. Do not edit by hand.
-- Introspected from the live sol::state after all bindings install.
-- Refreshed automatically on every dev launch (types/octarine_api.lua);
-- OCTARINE_DUMP_LUA_API=<path> overrides the destination.
-- NOTE: registry.get_<key>(e) and registry.try_get_<key>(e) return zero-copy
-- borrows into SoA archetype chunks; re-fetch after structural ECS mutations.
---@meta
---@diagnostic disable: lowercase-global, missing-return

---@class ImDrawFlags
---@field AAFill integer
---@field AALineEnds integer
---@field AALines integer
---@field CapSquare integer
---@field Closed integer
---@field JoinMiter integer
---@field None integer
---@field RoundAll integer
---@field RoundBottom integer
---@field RoundBottomLeft integer
---@field RoundBottomRight integer
---@field RoundCornersAll integer
---@field RoundCornersBottom integer
---@field RoundCornersBottomLeft integer
---@field RoundCornersBottomRight integer
---@field RoundCornersLeft integer
---@field RoundCornersNone integer
---@field RoundCornersRight integer
---@field RoundCornersTop integer
---@field RoundCornersTopLeft integer
---@field RoundCornersTopRight integer
---@field RoundLeft integer
---@field RoundNone integer
---@field RoundRight integer
---@field RoundTop integer
---@field RoundTopLeft integer
---@field RoundTopRight integer
---@field StrokeCenter integer
---@field StrokeCenterBiased integer
---@field StrokeInside integer
---@field StrokeLegacy integer
---@field StrokeOutside integer
---@field TextNoPixelSnap integer
---@field UseTexForRoundCorners integer
---@field UseVtxOffset integer
ImDrawFlags = {}


---@class ImDrawListFlags
---@field AllowVtxOffset integer
---@field AntiAliasedFill integer
---@field AntiAliasedLines integer
---@field AntiAliasedLinesUseTex integer
---@field None integer
---@field TextNoPixelSnap integer
ImDrawListFlags = {}


---@class ImFontAtlasFlags
---@field NoBakedLines integer
---@field NoBakedRoundCorners integer
---@field NoMouseCursors integer
---@field NoPowerOfTwoHeight integer
---@field None integer
ImFontAtlasFlags = {}


---@class ImFontFlags
---@field ImplicitRefSize integer
---@field LockBakedSizes integer
---@field NoLoadError integer
---@field NoLoadGlyphs integer
---@field None integer
ImFontFlags = {}


---@class ImGui
ImGui = {}

function ImGui.AlignTextToFramePadding(...) end

function ImGui.ArrowButton(...) end

function ImGui.Begin(...) end

function ImGui.BeginChild(...) end

function ImGui.BeginCombo(...) end

function ImGui.BeginDisabled(...) end

function ImGui.BeginDragDropSource(...) end

function ImGui.BeginDragDropTarget(...) end

function ImGui.BeginGroup(...) end

function ImGui.BeginItemTooltip(...) end

function ImGui.BeginListBox(...) end

function ImGui.BeginMainMenuBar(...) end

function ImGui.BeginMenu(...) end

function ImGui.BeginMenuBar(...) end

function ImGui.BeginPopup(...) end

function ImGui.BeginPopupContextItem(...) end

function ImGui.BeginPopupContextVoid(...) end

function ImGui.BeginPopupContextWindow(...) end

function ImGui.BeginPopupModal(...) end

function ImGui.BeginTabBar(...) end

function ImGui.BeginTabItem(...) end

function ImGui.BeginTable(...) end

function ImGui.BeginTooltip(...) end

function ImGui.Bullet(...) end

function ImGui.BulletText(...) end

function ImGui.Button(...) end

function ImGui.CalcItemWidth(...) end

function ImGui.CalcTextSize(...) end

function ImGui.Checkbox(...) end

function ImGui.CheckboxFlags(...) end

function ImGui.CloseCurrentPopup(...) end

function ImGui.CollapsingHeader(...) end

function ImGui.ColorButton(...) end

function ImGui.ColorConvertFloat4ToU32(...) end

function ImGui.ColorConvertHSVtoRGB(...) end

function ImGui.ColorConvertRGBtoHSV(...) end

function ImGui.ColorConvertU32ToFloat4(...) end

function ImGui.ColorEdit3(...) end

function ImGui.ColorEdit4(...) end

function ImGui.ColorPicker3(...) end

function ImGui.ColorPicker4(...) end

function ImGui.Columns(...) end

function ImGui.Combo(...) end

function ImGui.DebugCheckVersionAndDataLayout(...) end

function ImGui.DebugFlashStyleColor(...) end

function ImGui.DebugStartItemPicker(...) end

function ImGui.DebugTextEncoding(...) end

function ImGui.DragFloat(...) end

function ImGui.DragFloat2(...) end

function ImGui.DragFloat3(...) end

function ImGui.DragFloat4(...) end

function ImGui.DragFloatRange2(...) end

function ImGui.DragInt(...) end

function ImGui.DragInt2(...) end

function ImGui.DragInt3(...) end

function ImGui.DragInt4(...) end

function ImGui.DragIntRange2(...) end

function ImGui.Dummy(...) end

function ImGui.End(...) end

function ImGui.EndChild(...) end

function ImGui.EndCombo(...) end

function ImGui.EndDisabled(...) end

function ImGui.EndDragDropSource(...) end

function ImGui.EndDragDropTarget(...) end

function ImGui.EndFrame(...) end

function ImGui.EndGroup(...) end

function ImGui.EndListBox(...) end

function ImGui.EndMainMenuBar(...) end

function ImGui.EndMenu(...) end

function ImGui.EndMenuBar(...) end

function ImGui.EndPopup(...) end

function ImGui.EndTabBar(...) end

function ImGui.EndTabItem(...) end

function ImGui.EndTable(...) end

function ImGui.EndTooltip(...) end

function ImGui.GetClipboardText(...) end

function ImGui.GetColorU32(...) end

function ImGui.GetColumnIndex(...) end

function ImGui.GetColumnOffset(...) end

function ImGui.GetColumnWidth(...) end

function ImGui.GetColumnsCount(...) end

function ImGui.GetContentRegionAvail(...) end

function ImGui.GetContentRegionMax(...) end

function ImGui.GetCursorPos(...) end

function ImGui.GetCursorPosX(...) end

function ImGui.GetCursorPosY(...) end

function ImGui.GetCursorScreenPos(...) end

function ImGui.GetCursorStartPos(...) end

function ImGui.GetFontSize(...) end

function ImGui.GetFontTexUvWhitePixel(...) end

function ImGui.GetFrameCount(...) end

function ImGui.GetFrameHeight(...) end

function ImGui.GetFrameHeightWithSpacing(...) end

function ImGui.GetID(...) end

function ImGui.GetItemClickedCountWithSingleClickDelay(...) end

function ImGui.GetItemFlags(...) end

function ImGui.GetItemID(...) end

function ImGui.GetItemRectMax(...) end

function ImGui.GetItemRectMin(...) end

function ImGui.GetItemRectSize(...) end

function ImGui.GetKeyName(...) end

function ImGui.GetKeyPressedAmount(...) end

function ImGui.GetMouseClickedCount(...) end

function ImGui.GetMouseCursor(...) end

function ImGui.GetMouseDragDelta(...) end

function ImGui.GetMousePos(...) end

function ImGui.GetMousePosOnOpeningCurrentPopup(...) end

function ImGui.GetScrollMaxX(...) end

function ImGui.GetScrollMaxY(...) end

function ImGui.GetScrollX(...) end

function ImGui.GetScrollY(...) end

function ImGui.GetStyleColorName(...) end

function ImGui.GetStyleColorVec4(...) end

function ImGui.GetTextLineHeight(...) end

function ImGui.GetTextLineHeightWithSpacing(...) end

function ImGui.GetTime(...) end

function ImGui.GetTreeNodeToLabelSpacing(...) end

function ImGui.GetVersion(...) end

function ImGui.GetWindowContentRegionMax(...) end

function ImGui.GetWindowContentRegionMin(...) end

function ImGui.GetWindowHeight(...) end

function ImGui.GetWindowPos(...) end

function ImGui.GetWindowSize(...) end

function ImGui.GetWindowWidth(...) end

function ImGui.Indent(...) end

function ImGui.InputDouble(...) end

function ImGui.InputFloat(...) end

function ImGui.InputFloat2(...) end

function ImGui.InputFloat3(...) end

function ImGui.InputFloat4(...) end

function ImGui.InputInt(...) end

function ImGui.InputInt2(...) end

function ImGui.InputInt3(...) end

function ImGui.InputInt4(...) end

function ImGui.InputText(...) end

function ImGui.InputTextMultiline(...) end

function ImGui.InputTextWithHint(...) end

function ImGui.InvisibleButton(...) end

function ImGui.IsAnyItemActive(...) end

function ImGui.IsAnyItemFocused(...) end

function ImGui.IsAnyItemHovered(...) end

function ImGui.IsAnyMouseDown(...) end

function ImGui.IsItemActivated(...) end

function ImGui.IsItemActive(...) end

function ImGui.IsItemClicked(...) end

function ImGui.IsItemDeactivated(...) end

function ImGui.IsItemDeactivatedAfterEdit(...) end

function ImGui.IsItemEdited(...) end

function ImGui.IsItemFocused(...) end

function ImGui.IsItemHovered(...) end

function ImGui.IsItemToggledOpen(...) end

function ImGui.IsItemToggledSelection(...) end

function ImGui.IsItemVisible(...) end

function ImGui.IsKeyChordPressed(...) end

function ImGui.IsKeyDown(...) end

function ImGui.IsKeyPressed(...) end

function ImGui.IsKeyReleased(...) end

function ImGui.IsMouseClicked(...) end

function ImGui.IsMouseDoubleClicked(...) end

function ImGui.IsMouseDown(...) end

function ImGui.IsMouseDragging(...) end

function ImGui.IsMouseHoveringRect(...) end

function ImGui.IsMousePosValid(...) end

function ImGui.IsMouseReleased(...) end

function ImGui.IsMouseReleasedWithDelay(...) end

function ImGui.IsPopupOpen(...) end

function ImGui.IsRectVisible(...) end

function ImGui.IsWindowAppearing(...) end

function ImGui.IsWindowCollapsed(...) end

function ImGui.IsWindowFocused(...) end

function ImGui.IsWindowHovered(...) end

function ImGui.LabelText(...) end

function ImGui.ListBox(...) end

function ImGui.LoadIniSettingsFromDisk(...) end

function ImGui.LoadIniSettingsFromMemory(...) end

function ImGui.LogButtons(...) end

function ImGui.LogFinish(...) end

function ImGui.LogText(...) end

function ImGui.LogToClipboard(...) end

function ImGui.LogToFile(...) end

function ImGui.LogToTTY(...) end

function ImGui.MenuItem(...) end

function ImGui.NewFrame(...) end

function ImGui.NewLine(...) end

function ImGui.NextColumn(...) end

function ImGui.OpenPopup(...) end

function ImGui.OpenPopupOnItemClick(...) end

function ImGui.PopButtonRepeat(...) end

function ImGui.PopClipRect(...) end

function ImGui.PopFont(...) end

function ImGui.PopID(...) end

function ImGui.PopItemFlag(...) end

function ImGui.PopItemWidth(...) end

function ImGui.PopStyleColor(...) end

function ImGui.PopStyleVar(...) end

function ImGui.PopTabStop(...) end

function ImGui.PopTextWrapPos(...) end

function ImGui.ProgressBar(...) end

function ImGui.PushButtonRepeat(...) end

function ImGui.PushClipRect(...) end

function ImGui.PushID(...) end

function ImGui.PushItemFlag(...) end

function ImGui.PushItemWidth(...) end

function ImGui.PushStyleColor(...) end

function ImGui.PushStyleVar(...) end

function ImGui.PushStyleVarX(...) end

function ImGui.PushStyleVarY(...) end

function ImGui.PushTabStop(...) end

function ImGui.PushTextWrapPos(...) end

function ImGui.RadioButton(...) end

function ImGui.Render(...) end

function ImGui.ResetMouseDragDelta(...) end

function ImGui.SameLine(...) end

function ImGui.SaveIniSettingsToDisk(...) end

function ImGui.Selectable(...) end

function ImGui.Separator(...) end

function ImGui.SeparatorText(...) end

function ImGui.SetClipboardText(...) end

function ImGui.SetColorEditOptions(...) end

function ImGui.SetColumnOffset(...) end

function ImGui.SetColumnWidth(...) end

function ImGui.SetCursorPos(...) end

function ImGui.SetCursorPosX(...) end

function ImGui.SetCursorPosY(...) end

function ImGui.SetCursorScreenPos(...) end

function ImGui.SetItemDefaultFocus(...) end

function ImGui.SetItemKeyOwner(...) end

function ImGui.SetItemTooltip(...) end

function ImGui.SetKeyboardFocusHere(...) end

function ImGui.SetMouseCursor(...) end

function ImGui.SetNavCursorVisible(...) end

function ImGui.SetNextFrameWantCaptureKeyboard(...) end

function ImGui.SetNextFrameWantCaptureMouse(...) end

function ImGui.SetNextItemAllowOverlap(...) end

function ImGui.SetNextItemOpen(...) end

function ImGui.SetNextItemShortcut(...) end

function ImGui.SetNextItemStorageID(...) end

function ImGui.SetNextItemWidth(...) end

function ImGui.SetNextWindowBgAlpha(...) end

function ImGui.SetNextWindowCollapsed(...) end

function ImGui.SetNextWindowContentSize(...) end

function ImGui.SetNextWindowFocus(...) end

function ImGui.SetNextWindowPos(...) end

function ImGui.SetNextWindowScroll(...) end

function ImGui.SetNextWindowSize(...) end

function ImGui.SetNextWindowSizeConstraints(...) end

function ImGui.SetScrollFromPosX(...) end

function ImGui.SetScrollFromPosY(...) end

function ImGui.SetScrollHereX(...) end

function ImGui.SetScrollHereY(...) end

function ImGui.SetScrollX(...) end

function ImGui.SetScrollY(...) end

function ImGui.SetTabItemClosed(...) end

function ImGui.SetTooltip(...) end

function ImGui.SetWindowCollapsed(...) end

function ImGui.SetWindowFocus(...) end

function ImGui.SetWindowFontScale(...) end

function ImGui.SetWindowPos(...) end

function ImGui.SetWindowSize(...) end

function ImGui.Shortcut(...) end

function ImGui.ShowFontSelector(...) end

function ImGui.ShowStyleSelector(...) end

function ImGui.ShowUserGuide(...) end

function ImGui.SliderAngle(...) end

function ImGui.SliderFloat(...) end

function ImGui.SliderFloat2(...) end

function ImGui.SliderFloat3(...) end

function ImGui.SliderFloat4(...) end

function ImGui.SliderInt(...) end

function ImGui.SliderInt2(...) end

function ImGui.SliderInt3(...) end

function ImGui.SliderInt4(...) end

function ImGui.SmallButton(...) end

function ImGui.Spacing(...) end

function ImGui.TabItemButton(...) end

function ImGui.TableAngledHeadersRow(...) end

function ImGui.TableGetColumnCount(...) end

function ImGui.TableGetColumnFlags(...) end

function ImGui.TableGetColumnIndex(...) end

function ImGui.TableGetColumnName(...) end

function ImGui.TableGetHoveredColumn(...) end

function ImGui.TableGetRowIndex(...) end

function ImGui.TableHeader(...) end

function ImGui.TableHeadersRow(...) end

function ImGui.TableNextColumn(...) end

function ImGui.TableNextRow(...) end

function ImGui.TableSetBgColor(...) end

function ImGui.TableSetColumnEnabled(...) end

function ImGui.TableSetColumnIndex(...) end

function ImGui.TableSetupColumn(...) end

function ImGui.TableSetupScrollFreeze(...) end

function ImGui.Text(...) end

function ImGui.TextColored(...) end

function ImGui.TextDisabled(...) end

function ImGui.TextLink(...) end

function ImGui.TextLinkOpenURL(...) end

function ImGui.TextUnformatted(...) end

function ImGui.TextWrapped(...) end

function ImGui.TreeNode(...) end

function ImGui.TreeNodeEx(...) end

function ImGui.TreeNodeGetOpen(...) end

function ImGui.TreePop(...) end

function ImGui.TreePush(...) end

function ImGui.Unindent(...) end

function ImGui.VSliderFloat(...) end

function ImGui.VSliderInt(...) end


---@class ImGuiBackendFlags
---@field HasGamepad integer
---@field HasMouseCursors integer
---@field HasSetMousePos integer
---@field None integer
---@field RendererHasTextures integer
---@field RendererHasVtxOffset integer
ImGuiBackendFlags = {}


---@class ImGuiButtonFlags
---@field AllowOverlap integer
---@field EnableNav integer
---@field MouseButtonLeft integer
---@field MouseButtonMiddle integer
---@field MouseButtonRight integer
---@field None integer
ImGuiButtonFlags = {}


---@class ImGuiChildFlags
---@field AlwaysAutoResize integer
---@field AlwaysUseWindowPadding integer
---@field AutoResizeX integer
---@field AutoResizeY integer
---@field Borders integer
---@field FrameStyle integer
---@field NavFlattened integer
---@field None integer
---@field ResizeX integer
---@field ResizeY integer
ImGuiChildFlags = {}


---@class ImGuiCol
---@field Border integer
---@field BorderShadow integer
---@field Button integer
---@field ButtonActive integer
---@field ButtonHovered integer
---@field COUNT integer
---@field CheckMark integer
---@field CheckboxSelectedBg integer
---@field ChildBg integer
---@field DragDropTarget integer
---@field DragDropTargetBg integer
---@field FrameBg integer
---@field FrameBgActive integer
---@field FrameBgHovered integer
---@field Header integer
---@field HeaderActive integer
---@field HeaderHovered integer
---@field InputTextCursor integer
---@field MenuBarBg integer
---@field ModalWindowDimBg integer
---@field NavCursor integer
---@field NavHighlight integer
---@field NavWindowingDimBg integer
---@field NavWindowingHighlight integer
---@field PlotHistogram integer
---@field PlotHistogramHovered integer
---@field PlotLines integer
---@field PlotLinesHovered integer
---@field PopupBg integer
---@field ResizeGrip integer
---@field ResizeGripActive integer
---@field ResizeGripHovered integer
---@field ScrollbarBg integer
---@field ScrollbarGrab integer
---@field ScrollbarGrabActive integer
---@field ScrollbarGrabHovered integer
---@field Separator integer
---@field SeparatorActive integer
---@field SeparatorHovered integer
---@field SliderGrab integer
---@field SliderGrabActive integer
---@field Tab integer
---@field TabActive integer
---@field TabDimmed integer
---@field TabDimmedSelected integer
---@field TabDimmedSelectedOverline integer
---@field TabHovered integer
---@field TabSelected integer
---@field TabSelectedOverline integer
---@field TabUnfocused integer
---@field TabUnfocusedActive integer
---@field TableBorderLight integer
---@field TableBorderStrong integer
---@field TableHeaderBg integer
---@field TableRowBg integer
---@field TableRowBgAlt integer
---@field Text integer
---@field TextDisabled integer
---@field TextLink integer
---@field TextSelectedBg integer
---@field TitleBg integer
---@field TitleBgActive integer
---@field TitleBgCollapsed integer
---@field TreeLines integer
---@field UnsavedMarker integer
---@field WindowBg integer
ImGuiCol = {}


---@class ImGuiColorEditFlags
---@field AlphaBar integer
---@field AlphaNoBg integer
---@field AlphaOpaque integer
---@field AlphaPreview integer
---@field AlphaPreviewHalf integer
---@field DisplayHSV integer
---@field DisplayHex integer
---@field DisplayRGB integer
---@field Float integer
---@field HDR integer
---@field InputHSV integer
---@field InputRGB integer
---@field NoAlpha integer
---@field NoBorder integer
---@field NoColorMarkers integer
---@field NoDragDrop integer
---@field NoInputs integer
---@field NoLabel integer
---@field NoOptions integer
---@field NoPicker integer
---@field NoSidePreview integer
---@field NoSmallPreview integer
---@field NoTooltip integer
---@field None integer
---@field PickerHueBar integer
---@field PickerHueWheel integer
---@field PickerNoRotate integer
---@field Uint8 integer
ImGuiColorEditFlags = {}


---@class ImGuiComboFlags
---@field HeightLarge integer
---@field HeightLargest integer
---@field HeightRegular integer
---@field HeightSmall integer
---@field NoArrowButton integer
---@field NoPreview integer
---@field None integer
---@field PopupAlignLeft integer
---@field WidthFitPreview integer
ImGuiComboFlags = {}


---@class ImGuiCond
---@field Always integer
---@field Appearing integer
---@field FirstUseEver integer
---@field None integer
---@field Once integer
ImGuiCond = {}


---@class ImGuiConfigFlags
---@field IsSRGB integer
---@field IsTouchScreen integer
---@field NavEnableGamepad integer
---@field NavEnableKeyboard integer
---@field NavEnableSetMousePos integer
---@field NavNoCaptureKeyboard integer
---@field NoKeyboard integer
---@field NoMouse integer
---@field NoMouseCursorChange integer
---@field None integer
ImGuiConfigFlags = {}


---@class ImGuiDataType
---@field Bool integer
---@field COUNT integer
---@field Double integer
---@field Float integer
---@field S16 integer
---@field S32 integer
---@field S64 integer
---@field S8 integer
---@field String integer
---@field U16 integer
---@field U32 integer
---@field U64 integer
---@field U8 integer
ImGuiDataType = {}


---@class ImGuiDir
---@field _COUNT integer
---@field _Down integer
---@field _Left integer
---@field _None integer
---@field _Right integer
---@field _Up integer
ImGuiDir = {}


---@class ImGuiDragDropFlags
---@field AcceptBeforeDelivery integer
---@field AcceptDrawAsHovered integer
---@field AcceptNoDrawDefaultRect integer
---@field AcceptNoPreviewTooltip integer
---@field AcceptPeekOnly integer
---@field None integer
---@field PayloadAutoExpire integer
---@field PayloadNoCrossContext integer
---@field PayloadNoCrossProcess integer
---@field SourceAllowNullID integer
---@field SourceExtern integer
---@field SourceNoDisableHover integer
---@field SourceNoHoldToOpenOthers integer
---@field SourceNoPreviewTooltip integer
ImGuiDragDropFlags = {}


---@class ImGuiFocusedFlags
---@field AnyWindow integer
---@field ChildWindows integer
---@field NoPopupHierarchy integer
---@field None integer
---@field RootAndChildWindows integer
---@field RootWindow integer
ImGuiFocusedFlags = {}


---@class ImGuiHoveredFlags
---@field AllowWhenBlockedByActiveItem integer
---@field AllowWhenBlockedByPopup integer
---@field AllowWhenDisabled integer
---@field AllowWhenOverlapped integer
---@field AllowWhenOverlappedByItem integer
---@field AllowWhenOverlappedByWindow integer
---@field AnyWindow integer
---@field ChildWindows integer
---@field DelayNone integer
---@field DelayNormal integer
---@field DelayShort integer
---@field ForTooltip integer
---@field NoNavOverride integer
---@field NoPopupHierarchy integer
---@field NoSharedDelay integer
---@field None integer
---@field RectOnly integer
---@field RootAndChildWindows integer
---@field RootWindow integer
---@field Stationary integer
ImGuiHoveredFlags = {}


---@class ImGuiInputFlags
---@field None integer
---@field Repeat integer
---@field RouteActive integer
---@field RouteAlways integer
---@field RouteFocused integer
---@field RouteFromRootWindow integer
---@field RouteGlobal integer
---@field RouteOverActive integer
---@field RouteOverFocused integer
---@field RouteUnlessBgFocused integer
---@field Tooltip integer
ImGuiInputFlags = {}


---@class ImGuiInputTextFlags
---@field AllowTabInput integer
---@field AlwaysOverwrite integer
---@field AutoSelectAll integer
---@field CallbackAlways integer
---@field CallbackCharFilter integer
---@field CallbackCompletion integer
---@field CallbackEdit integer
---@field CallbackHistory integer
---@field CallbackResize integer
---@field CharsDecimal integer
---@field CharsHexadecimal integer
---@field CharsNoBlank integer
---@field CharsScientific integer
---@field CharsUppercase integer
---@field CtrlEnterForNewLine integer
---@field DisplayEmptyRefVal integer
---@field ElideLeft integer
---@field EnterReturnsTrue integer
---@field EscapeClearsAll integer
---@field NoHorizontalScroll integer
---@field NoUndoRedo integer
---@field None integer
---@field ParseEmptyRefVal integer
---@field Password integer
---@field ReadOnly integer
---@field WordWrap integer
ImGuiInputTextFlags = {}


---@class ImGuiItemFlags
---@field AllowDuplicateId integer
---@field AutoClosePopups integer
---@field ButtonRepeat integer
---@field Disabled integer
---@field LiveEditOnInput integer
---@field LiveEditOnInputScalar integer
---@field LiveEditOnInputText integer
---@field MixedValue integer
---@field NoNav integer
---@field NoNavDefaultFocus integer
---@field NoTabStop integer
---@field None integer
ImGuiItemFlags = {}


---@class ImGuiKey
---@field ImGuiMod_Alt integer
---@field ImGuiMod_Ctrl integer
---@field ImGuiMod_None integer
---@field ImGuiMod_Shift integer
---@field ImGuiMod_Shortcut integer
---@field ImGuiMod_Super integer
---@field _0 integer
---@field _1 integer
---@field _2 integer
---@field _3 integer
---@field _4 integer
---@field _5 integer
---@field _6 integer
---@field _7 integer
---@field _8 integer
---@field _9 integer
---@field _A integer
---@field _Apostrophe integer
---@field _AppBack integer
---@field _AppForward integer
---@field _B integer
---@field _Backslash integer
---@field _Backspace integer
---@field _C integer
---@field _COUNT integer
---@field _CapsLock integer
---@field _Comma integer
---@field _D integer
---@field _Delete integer
---@field _DownArrow integer
---@field _E integer
---@field _End integer
---@field _Enter integer
---@field _Equal integer
---@field _Escape integer
---@field _F integer
---@field _F1 integer
---@field _F10 integer
---@field _F11 integer
---@field _F12 integer
---@field _F13 integer
---@field _F14 integer
---@field _F15 integer
---@field _F16 integer
---@field _F17 integer
---@field _F18 integer
---@field _F19 integer
---@field _F2 integer
---@field _F20 integer
---@field _F21 integer
---@field _F22 integer
---@field _F23 integer
---@field _F24 integer
---@field _F3 integer
---@field _F4 integer
---@field _F5 integer
---@field _F6 integer
---@field _F7 integer
---@field _F8 integer
---@field _F9 integer
---@field _G integer
---@field _GamepadBack integer
---@field _GamepadDpadDown integer
---@field _GamepadDpadLeft integer
---@field _GamepadDpadRight integer
---@field _GamepadDpadUp integer
---@field _GamepadFaceDown integer
---@field _GamepadFaceLeft integer
---@field _GamepadFaceRight integer
---@field _GamepadFaceUp integer
---@field _GamepadL1 integer
---@field _GamepadL2 integer
---@field _GamepadL3 integer
---@field _GamepadLStickDown integer
---@field _GamepadLStickLeft integer
---@field _GamepadLStickRight integer
---@field _GamepadLStickUp integer
---@field _GamepadR1 integer
---@field _GamepadR2 integer
---@field _GamepadR3 integer
---@field _GamepadRStickDown integer
---@field _GamepadRStickLeft integer
---@field _GamepadRStickRight integer
---@field _GamepadRStickUp integer
---@field _GamepadStart integer
---@field _GraveAccent integer
---@field _H integer
---@field _Home integer
---@field _I integer
---@field _Insert integer
---@field _J integer
---@field _K integer
---@field _Keypad0 integer
---@field _Keypad1 integer
---@field _Keypad2 integer
---@field _Keypad3 integer
---@field _Keypad4 integer
---@field _Keypad5 integer
---@field _Keypad6 integer
---@field _Keypad7 integer
---@field _Keypad8 integer
---@field _Keypad9 integer
---@field _KeypadAdd integer
---@field _KeypadDecimal integer
---@field _KeypadDivide integer
---@field _KeypadEnter integer
---@field _KeypadEqual integer
---@field _KeypadMultiply integer
---@field _KeypadSubtract integer
---@field _L integer
---@field _LeftAlt integer
---@field _LeftArrow integer
---@field _LeftBracket integer
---@field _LeftCtrl integer
---@field _LeftShift integer
---@field _LeftSuper integer
---@field _M integer
---@field _Menu integer
---@field _Minus integer
---@field _MouseLeft integer
---@field _MouseMiddle integer
---@field _MouseRight integer
---@field _MouseWheelX integer
---@field _MouseWheelY integer
---@field _MouseX1 integer
---@field _MouseX2 integer
---@field _N integer
---@field _NamedKey_BEGIN integer
---@field _None integer
---@field _NumLock integer
---@field _O integer
---@field _Oem102 integer
---@field _P integer
---@field _PageDown integer
---@field _PageUp integer
---@field _Pause integer
---@field _Period integer
---@field _PrintScreen integer
---@field _Q integer
---@field _R integer
---@field _RightAlt integer
---@field _RightArrow integer
---@field _RightBracket integer
---@field _RightCtrl integer
---@field _RightShift integer
---@field _RightSuper integer
---@field _S integer
---@field _ScrollLock integer
---@field _Semicolon integer
---@field _Slash integer
---@field _Space integer
---@field _T integer
---@field _Tab integer
---@field _U integer
---@field _UpArrow integer
---@field _V integer
---@field _W integer
---@field _X integer
---@field _Y integer
---@field _Z integer
ImGuiKey = {}


---@class ImGuiListClipperFlags
---@field NoSetTableRowCounters integer
---@field None integer
ImGuiListClipperFlags = {}


---@class ImGuiMouseButton
---@field COUNT integer
---@field Left integer
---@field Middle integer
---@field Right integer
ImGuiMouseButton = {}


---@class ImGuiMouseCursor
---@field Arrow integer
---@field COUNT integer
---@field Hand integer
---@field None integer
---@field NotAllowed integer
---@field Progress integer
---@field ResizeAll integer
---@field ResizeEW integer
---@field ResizeNESW integer
---@field ResizeNS integer
---@field ResizeNWSE integer
---@field TextInput integer
---@field Wait integer
ImGuiMouseCursor = {}


---@class ImGuiMouseSource
---@field _COUNT integer
---@field _Mouse integer
---@field _Pen integer
---@field _TouchScreen integer
ImGuiMouseSource = {}


---@class ImGuiMultiSelectFlags
---@field BoxSelect1d integer
---@field BoxSelect2d integer
---@field BoxSelectNoScroll integer
---@field ClearOnClickVoid integer
---@field ClearOnEscape integer
---@field NavWrapX integer
---@field NoAutoClear integer
---@field NoAutoClearOnReselect integer
---@field NoAutoSelect integer
---@field NoRangeSelect integer
---@field NoSelectAll integer
---@field NoSelectOnRightClick integer
---@field None integer
---@field ScopeRect integer
---@field ScopeWindow integer
---@field SelectOnAuto integer
---@field SelectOnClick integer
---@field SelectOnClickAlways integer
---@field SelectOnClickRelease integer
---@field SingleSelect integer
ImGuiMultiSelectFlags = {}


---@class ImGuiPopupFlags
---@field AnyPopup integer
---@field AnyPopupId integer
---@field AnyPopupLevel integer
---@field MouseButtonLeft integer
---@field MouseButtonMiddle integer
---@field MouseButtonRight integer
---@field NoOpenOverExistingPopup integer
---@field NoOpenOverItems integer
---@field NoReopen integer
---@field None integer
ImGuiPopupFlags = {}


---@class ImGuiSelectableFlags
---@field AllowDoubleClick integer
---@field AllowOverlap integer
---@field Disabled integer
---@field DontClosePopups integer
---@field Highlight integer
---@field NoAutoClosePopups integer
---@field None integer
---@field SelectOnNav integer
---@field SpanAllColumns integer
ImGuiSelectableFlags = {}


---@class ImGuiSelectionRequestType
---@field _None integer
---@field _SetAll integer
---@field _SetRange integer
ImGuiSelectionRequestType = {}


---@class ImGuiSliderFlags
---@field AlwaysClamp integer
---@field ClampOnInput integer
---@field ClampZeroRange integer
---@field ColorMarkers integer
---@field Logarithmic integer
---@field NoInput integer
---@field NoRoundToFormat integer
---@field NoSpeedTweaks integer
---@field None integer
---@field WrapAround integer
ImGuiSliderFlags = {}


---@class ImGuiSortDirection
---@field _Ascending integer
---@field _Descending integer
---@field _None integer
ImGuiSortDirection = {}


---@class ImGuiStyleVar
---@field Alpha integer
---@field ButtonTextAlign integer
---@field COUNT integer
---@field CellPadding integer
---@field ChildBorderSize integer
---@field ChildRounding integer
---@field DisabledAlpha integer
---@field DragDropTargetRounding integer
---@field FrameBorderSize integer
---@field FramePadding integer
---@field FrameRounding integer
---@field GrabMinSize integer
---@field GrabRounding integer
---@field ImageBorderSize integer
---@field ImageRounding integer
---@field IndentSpacing integer
---@field ItemInnerSpacing integer
---@field ItemSpacing integer
---@field MenuItemRounding integer
---@field PopupBorderSize integer
---@field PopupRounding integer
---@field ScrollbarPadding integer
---@field ScrollbarRounding integer
---@field ScrollbarSize integer
---@field SelectableRounding integer
---@field SelectableTextAlign integer
---@field SeparatorSize integer
---@field SeparatorTextAlign integer
---@field SeparatorTextBorderSize integer
---@field SeparatorTextPadding integer
---@field TabBarBorderSize integer
---@field TabBarOverlineSize integer
---@field TabBorderSize integer
---@field TabMinWidthBase integer
---@field TabMinWidthShrink integer
---@field TabRounding integer
---@field TableAngledHeadersAngle integer
---@field TableAngledHeadersTextAlign integer
---@field TreeLinesRounding integer
---@field TreeLinesSize integer
---@field WindowBorderSize integer
---@field WindowMinSize integer
---@field WindowPadding integer
---@field WindowRounding integer
---@field WindowTitleAlign integer
ImGuiStyleVar = {}


---@class ImGuiTabBarFlags
---@field AutoSelectNewTabs integer
---@field DrawSelectedOverline integer
---@field FittingPolicyMixed integer
---@field FittingPolicyResizeDown integer
---@field FittingPolicyScroll integer
---@field FittingPolicyShrink integer
---@field NoCloseWithMiddleMouseButton integer
---@field NoTabListScrollingButtons integer
---@field NoTooltip integer
---@field None integer
---@field Reorderable integer
---@field TabListPopupButton integer
ImGuiTabBarFlags = {}


---@class ImGuiTabItemFlags
---@field Leading integer
---@field NoAssumedClosure integer
---@field NoCloseWithMiddleMouseButton integer
---@field NoPushId integer
---@field NoReorder integer
---@field NoTooltip integer
---@field None integer
---@field SetSelected integer
---@field Trailing integer
---@field UnsavedDocument integer
ImGuiTabItemFlags = {}


---@class ImGuiTableBgTarget
---@field CellBg integer
---@field None integer
---@field RowBg0 integer
---@field RowBg1 integer
ImGuiTableBgTarget = {}


---@class ImGuiTableColumnFlags
---@field AngledHeader integer
---@field DefaultHide integer
---@field DefaultSort integer
---@field Disabled integer
---@field IndentDisable integer
---@field IndentEnable integer
---@field IsEnabled integer
---@field IsHovered integer
---@field IsSorted integer
---@field IsVisible integer
---@field NoClip integer
---@field NoHeaderLabel integer
---@field NoHeaderWidth integer
---@field NoHide integer
---@field NoReorder integer
---@field NoResize integer
---@field NoSort integer
---@field NoSortAscending integer
---@field NoSortDescending integer
---@field None integer
---@field PreferSortAscending integer
---@field PreferSortDescending integer
---@field WidthFixed integer
---@field WidthStretch integer
ImGuiTableColumnFlags = {}


---@class ImGuiTableFlags
---@field Borders integer
---@field BordersH integer
---@field BordersInner integer
---@field BordersInnerH integer
---@field BordersInnerV integer
---@field BordersOuter integer
---@field BordersOuterH integer
---@field BordersOuterV integer
---@field BordersV integer
---@field ContextMenuInBody integer
---@field Hideable integer
---@field HighlightHoveredColumn integer
---@field NoBordersInBody integer
---@field NoBordersInBodyUntilResize integer
---@field NoClip integer
---@field NoHostExtendX integer
---@field NoHostExtendY integer
---@field NoKeepColumnsVisible integer
---@field NoPadInnerX integer
---@field NoPadOuterX integer
---@field NoSavedSettings integer
---@field None integer
---@field PadOuterX integer
---@field PreciseWidths integer
---@field Reorderable integer
---@field Resizable integer
---@field RowBg integer
---@field ScrollX integer
---@field ScrollY integer
---@field SizingFixedFit integer
---@field SizingFixedSame integer
---@field SizingStretchProp integer
---@field SizingStretchSame integer
---@field SortMulti integer
---@field SortTristate integer
---@field Sortable integer
ImGuiTableFlags = {}


---@class ImGuiTableRowFlags
---@field Headers integer
---@field None integer
ImGuiTableRowFlags = {}


---@class ImGuiTreeNodeFlags
---@field AllowOverlap integer
---@field Bullet integer
---@field CollapsingHeader integer
---@field DefaultOpen integer
---@field DrawLinesFull integer
---@field DrawLinesNone integer
---@field DrawLinesToNodes integer
---@field FramePadding integer
---@field Framed integer
---@field LabelSpanAllColumns integer
---@field Leaf integer
---@field NavLeftJumpsBackHere integer
---@field NavLeftJumpsToParent integer
---@field NoAutoOpenOnLog integer
---@field NoTreePushOnOpen integer
---@field None integer
---@field OpenOnArrow integer
---@field OpenOnDoubleClick integer
---@field Selected integer
---@field SpanAllColumns integer
---@field SpanAvailWidth integer
---@field SpanFullWidth integer
---@field SpanLabelWidth integer
ImGuiTreeNodeFlags = {}


---@class ImGuiWindowFlags
---@field AlwaysAutoResize integer
---@field AlwaysHorizontalScrollbar integer
---@field AlwaysVerticalScrollbar integer
---@field HorizontalScrollbar integer
---@field MenuBar integer
---@field NoBackground integer
---@field NoBringToFrontOnFocus integer
---@field NoCollapse integer
---@field NoDecoration integer
---@field NoFocusOnAppearing integer
---@field NoInputs integer
---@field NoMouseInputs integer
---@field NoMove integer
---@field NoNav integer
---@field NoNavFocus integer
---@field NoNavInputs integer
---@field NoResize integer
---@field NoSavedSettings integer
---@field NoScrollWithMouse integer
---@field NoScrollbar integer
---@field NoTitleBar integer
---@field None integer
---@field UnsavedDocument integer
ImGuiWindowFlags = {}


---@class ImTextureFormat
---@field _Alpha8 integer
---@field _RGBA32 integer
ImTextureFormat = {}


---@class ImTextureStatus
---@field _Destroyed integer
---@field _OK integer
---@field _WantCreate integer
---@field _WantDestroy integer
---@field _WantUpdates integer
ImTextureStatus = {}


---@class ImVec2
ImVec2 = {}


---@class ImVec4
ImVec4 = {}


---@param scene_path? string
---@return integer
function acquire_scene_assets(scene_path) end

---@param arg1 entity
---@param arg2 string
function add_tag(arg1, arg2) end

---@class animation_component
---@field current_frame integer
---@field frame_rate_speed integer
---@field is_finished boolean
---@field num_frames integer
---@field should_loop boolean
animation_component = {}

---@param ... any
---@return animation_component
function animation_component.new(...) end

---@param arg1 entity
---@param arg2 entity
---@return boolean
function are_colliding(arg1, arg2) end

---@class audio_listener_component
---@field doppler_factor number
---@field max_distance number
---@field rolloff number
---@field speed_of_sound number
audio_listener_component = {}

---@param ... any
---@return audio_listener_component
function audio_listener_component.new(...) end

---@class audio_source_component
---@field clip_id string
---@field despawn_on_finish boolean
---@field doppler boolean
---@field loop boolean
---@field max_distance number
---@field min_distance number
---@field pitch number
---@field play_on_spawn boolean
---@field spatial boolean
---@field volume number
audio_source_component = {}

---@param ... any
---@return audio_source_component
function audio_source_component.new(...) end

---@overload fun(arg1: table)
---@param arg1 entity
function blam(arg1) end

---@class box_collider_component
---@field height integer
---@field is_fixed boolean
---@field offset vec2
---@field width integer
box_collider_component = {}

---@param ... any
---@return box_collider_component
function box_collider_component.new(...) end

---@class camera_follow_component
camera_follow_component = {}

---@param ... any
---@return camera_follow_component
function camera_follow_component.new(...) end

---@class capsule_collider_component
---@field end vec2
---@field is_fixed boolean
---@field radius number
---@field start vec2
capsule_collider_component = {}

---@param ... any
---@return capsule_collider_component
function capsule_collider_component.new(...) end

---@class capsule_primitive_component
---@field blend_mode string
---@field color color
---@field end vec2
---@field fill_ratio number
---@field filled boolean
---@field is_fixed boolean
---@field layer integer
---@field radius number
---@field start vec2
---@field thickness number
capsule_primitive_component = {}

---@param ... any
---@return capsule_primitive_component
function capsule_primitive_component.new(...) end

---@class chunk_resident_component
---@field chunk_x integer
---@field chunk_y integer
---@field prop_index integer
chunk_resident_component = {}

---@param ... any
---@return chunk_resident_component
function chunk_resident_component.new(...) end

---@class circle_collider_component
---@field is_fixed boolean
---@field offset vec2
---@field radius number
circle_collider_component = {}

---@param ... any
---@return circle_collider_component
function circle_collider_component.new(...) end

---@class circle_primitive_component
---@field blend_mode string
---@field color color
---@field filled boolean
---@field is_fixed boolean
---@field layer integer
---@field position vec2
---@field radius number
---@field thickness number
circle_primitive_component = {}

---@param ... any
---@return circle_primitive_component
function circle_primitive_component.new(...) end

---@param arg1? string
function clear_import_cache(arg1) end

function clear_scene() end

---@class color
---@field a integer
---@field b integer
---@field g integer
---@field r integer
color = {}

---@param ... any
---@return color
function color.new(...) end

---@class color_grid_component
---@field blend_mode string
---@field bounds vec2
---@field cell_height number
---@field cell_size number
---@field cell_width number
---@field color1 color
---@field color2 color
---@field color_a color
---@field color_b color
---@field is_fixed boolean
---@field layer integer
---@field offset vec2
---@field scroll_velocity vec2
color_grid_component = {}

---@param ... any
---@return color_grid_component
function color_grid_component.new(...) end

---@class dev
dev = {}

---@param arg1 string
---@return boolean
function dev.apply_ui_stress_preset(arg1) end

---@return table
function dev.audit_ui() end

---@return string
function dev.audit_ui_json() end

function dev.clear() end

---@param arg1 string
---@param arg2? boolean
---@return boolean
function dev.delete_ui_preset(arg1, arg2) end

---@return string
function dev.export_ui_mock_lua() end

---@return entity?
function dev.get_selected_entity() end

---@return string
function dev.get_ui_viewport_json() end

---@param arg1? string
---@return table
function dev.inspect_ui_contract(arg1) end

---@param arg1? string
---@return string
function dev.inspect_ui_contract_json(arg1) end

---@param arg1 string
---@return boolean
function dev.is_panel_open(arg1) end

---@return string
function dev.list_ui_presets_json() end

---@return table
function dev.list_ui_views() end

---@param arg1 string
---@return boolean
function dev.load_ui_mock(arg1) end

---@param arg1 string
---@param arg2? string
---@return boolean
function dev.preview_inline_ui(arg1, arg2) end

---@param arg1 string
---@param arg2? table
---@return boolean
function dev.preview_ui(arg1, arg2) end

---@param arg1 string
---@param arg2 fun(...)
---@param arg3? string
function dev.register_action(arg1, arg2, arg3) end

---@param arg1 string
---@param arg2 any
function dev.register_panel(arg1, arg2) end

---@param arg1 string
---@param arg2 fun(...)
---@param arg3 fun(...)
function dev.register_toggle(arg1, arg2, arg3) end

---@return boolean
function dev.reload_preview_ui() end

---@param arg1 string
---@param arg2 integer
---@return boolean
function dev.resize_ui_mock_array(arg1, arg2) end

---@param arg1 string
---@param arg2? string
---@param arg3? string
---@return string
function dev.run_ui_matrix_json(arg1, arg2, arg3) end

---@param arg1? string
---@param arg2? boolean
---@return boolean
function dev.save_ui_mock(arg1, arg2) end

---@param arg1 string
---@param arg2? boolean
---@param arg3? boolean
---@return boolean
function dev.save_ui_preset(arg1, arg2, arg3) end

---@param arg1? string
---@return boolean
function dev.seed_ui_mock(arg1) end

---@param arg1? entity
function dev.select_entity(arg1) end

---@param arg1 string
---@return boolean
function dev.select_ui_preset(arg1) end

---@param arg1 string
---@param arg2 boolean
function dev.set_panel_open(arg1, arg2) end

---@param arg1 boolean
---@return boolean
function dev.set_ui_integer_zoom(arg1) end

---@param arg1 string
---@param arg2 any
---@return boolean
function dev.set_ui_mock_field(arg1, arg2) end

---@param arg1 string
---@param arg2 any
---@return boolean
function dev.set_ui_param(arg1, arg2) end

---@param arg1 boolean
---@return boolean
function dev.set_ui_safe_area_guides(arg1) end

---@param arg1 integer
---@param arg2 integer
---@return boolean
function dev.set_ui_viewport(arg1, arg2) end

function dev.unmount_preview_ui() end

---@param arg1 string
---@return boolean
function dev.unregister_panel(arg1) end

---@param arg1 string
---@return boolean
function dev.unwatch(arg1) end

---@param arg1 string
---@param arg2 fun(...)
function dev.watch(arg1, arg2) end


---@class entity
entity = {}

---@param ... any
---@return entity
function entity.new(...) end

---@return integer
function entity:get_id() end

---@param arg1 string
---@return any
function find_entity_by_name(arg1) end

---@param arg1 string
---@return string
function get_asset_path(arg1) end

---@return number
function get_avg_fps() end

---@return integer, integer
function get_base_resolution() end

---@return vec2
function get_camera_position() end

---@return number, number
function get_camera_position_xy() end

---@return number
function get_camera_zoom() end

---@return integer
function get_current_display_index() end

---@return string
function get_current_display_name() end

---@return integer
function get_display_count() end

---@param arg1? integer
---@return table
function get_display_modes(arg1) end

---@param arg1? integer
---@return integer, integer
function get_display_size(arg1) end

---@return table
function get_displays() end

---@param arg1 entity
---@return table
function get_entity_children(arg1) end

---@return integer
function get_entity_count() end

---@param arg1 string
---@return string?
function get_env(arg1) end

---@return number
function get_fixed_dt() end

---@return number
function get_fps() end

---@return integer
function get_fps_target() end

---@return number
function get_frame_time() end

---@return integer, integer, number
function get_fullscreen_resolution() end

---@return vec2
function get_game_map_dimensions() end

---@return number, number
function get_game_map_dimensions_xy() end

---@return integer, integer
function get_logical_size() end

---@return number
function get_master_volume() end

---@return integer
function get_memory_usage() end

---@param arg1 entity
---@return string
function get_name(arg1) end

---@return table
function get_perf_stats() end

---@param arg1 entity
---@return vec2
function get_position(arg1) end

---@param arg1 entity
---@return number, number
function get_position_xy(arg1) end

---@return string
function get_presentation_mode() end

---@return integer
function get_sim_substeps() end

---@param arg1 entity
---@return number, number, number, number
function get_sprite_src_rect(arg1) end

---@return string
function get_state_hash() end

---@return number
function get_system_time() end

---@param arg1 entity
---@return string
function get_text_label_font(arg1) end

---@return string
function get_texture_scale_mode() end

---@return string
function get_timestamp_string() end

---@param arg1 string
---@param arg2? string
---@return string
function get_user_setting(arg1, arg2) end

---@return boolean
function get_vsync() end

---@return string
function get_window_mode() end

---@return integer, integer
function get_window_position() end

---@return integer, integer
function get_window_size() end

---@param arg1 string
---@return boolean
function has_catalog_asset(arg1) end

---@param arg1 entity
---@param arg2 string
---@return boolean
function has_tag(arg1, arg2) end

---@return boolean
function has_verify_failed() end

---@class hot_reload
hot_reload = {}

---@return integer
function hot_reload.get_tracked_count() end

---@param arg1? number
---@return table
function hot_reload.poll_scripts(arg1) end

---@param arg1 string
function hot_reload.track_script(arg1) end

---@param arg1 string
function hot_reload.untrack_script(arg1) end


---@param arg1 string
---@param arg2? boolean
---@return any
function import(arg1, arg2) end

---@class input
input = {}

---@param arg1 string
---@param arg2 string
function input.bind(arg1, arg2) end

---@param arg1 string
---@param arg2 string
function input.bind_gamepad(arg1, arg2) end

---@param arg1 string
---@param arg2 string
function input.bind_key(arg1, arg2) end

---@param arg1 string
---@param arg2? number
---@return number
function input.gamepad_axis(arg1, arg2) end

---@param arg1 string
---@param arg2? number
---@return vec2
function input.gamepad_stick(arg1, arg2) end

---@param arg1 string
---@param arg2? number
---@return number, number
function input.gamepad_stick_xy(arg1, arg2) end

---@param arg1? string
---@return table
function input.get_bindings(arg1) end

---@param arg1 string
---@return boolean
function input.is_action_down(arg1) end

---@param arg1 string
---@return boolean
function input.is_action_pressed(arg1) end

---@param arg1 string
---@return boolean
function input.is_action_released(arg1) end

---@return boolean
function input.is_focused() end

---@return boolean
function input.is_gamepad_connected() end

---@param arg1 string
---@return boolean
function input.is_gamepad_down(arg1) end

---@param arg1 string
---@return boolean
function input.is_gamepad_pressed(arg1) end

---@param arg1 string
---@return boolean
function input.is_gamepad_released(arg1) end

---@return boolean
function input.is_hovered() end

---@param arg1 string
---@return boolean
function input.is_key_down(arg1) end

---@param arg1 string
---@return boolean
function input.is_key_pressed(arg1) end

---@param arg1 string
---@return boolean
function input.is_key_released(arg1) end

---@param arg1 any
---@return boolean
function input.is_mouse_down(arg1) end

---@param arg1 any
---@return boolean
function input.is_mouse_pressed(arg1) end

---@param arg1 any
---@return boolean
function input.is_mouse_released(arg1) end

---@return vec2
function input.mouse_position() end

---@return number, number
function input.mouse_position_xy() end

---@return vec2
function input.mouse_wheel() end

---@return number, number
function input.mouse_wheel_xy() end

---@param arg1 fun(...)
function input.on_gamepad_down(arg1) end

---@param arg1 fun(...)
function input.on_gamepad_up(arg1) end

---@param arg1 fun(...)
function input.on_key_down(arg1) end

---@param arg1 fun(...)
function input.on_key_up(arg1) end

---@param arg1 fun(...)
function input.on_mouse_down(arg1) end

---@param arg1 fun(...)
function input.on_mouse_up(arg1) end

---@param arg1 fun(...)
function input.on_mouse_wheel(arg1) end

---@param arg1 any
---@param arg2? table
function input.set_bindings(arg1, arg2) end

---@param arg1 string
---@param arg2? string
function input.unbind(arg1, arg2) end

---@param arg1 string
---@param arg2? string
function input.unbind_gamepad(arg1, arg2) end

---@param arg1 string
---@param arg2? string
function input.unbind_key(arg1, arg2) end


---@return boolean
function is_audio_muted() end

---@return boolean
function is_deterministic() end

---@return boolean
function is_fullscreen() end

---@return boolean
function is_headless() end

---@return boolean
function is_hot_reload_enabled() end

---@return boolean
function is_maximized() end

---@return boolean
function is_shipped_build() end

---@return boolean
function is_window_resizable() end

---@class lifetime_component
---@field duration number
---@field remaining_duration number
lifetime_component = {}

---@param ... any
---@return lifetime_component
function lifetime_component.new(...) end

---@param arg1 number
function lifetime_component:extend(arg1) end

---@return boolean
function lifetime_component:is_alive() end

function lifetime_component:reset() end

---@class line_collider_component
---@field end vec2
---@field is_fixed boolean
---@field start vec2
line_collider_component = {}

---@param ... any
---@return line_collider_component
function line_collider_component.new(...) end

---@class line_primitive_component
---@field blend_mode string
---@field color color
---@field end vec2
---@field is_fixed boolean
---@field layer integer
---@field start vec2
---@field thickness number
line_primitive_component = {}

---@param ... any
---@return line_primitive_component
function line_primitive_component.new(...) end

---@param descriptor table
---@return boolean
function load_asset(descriptor) end

---@param entity_path string
---@return entity
function load_entity(entity_path) end

---@param scene_path string
---@return boolean
function load_scene(scene_path) end

---@param arg1 string
function log(arg1) end

---@param arg1 string
function log_e(arg1) end

---@param arg1 string
function log_i(arg1) end

---@param arg1 table
---@param arg2? string
function log_table(arg1, arg2) end

---@param arg1 string
function log_w(arg1) end

---@param arg1 string
function mark_verify_failed(arg1) end

---@class name_component
---@field name string
name_component = {}

---@param ... any
---@return name_component
function name_component.new(...) end

---@type boolean
oct_shipped = false

---@param arg1 string
---@return boolean
function open_external_url(arg1) end

---@class pivot_component
---@field value vec2
---@field x number
---@field y number
pivot_component = {}

---@param ... any
---@return pivot_component
function pivot_component.new(...) end

---@param arg1 string
---@param arg2? number
function play_sound(arg1, arg2) end

---@class position_component
---@field value vec2
---@field x number
---@field y number
position_component = {}

---@param ... any
---@return position_component
function position_component.new(...) end

function quit_game() end

---@param arg1 string
---@return string[]
function read_file_lines(arg1) end

---@class registry
registry = {}

---@param arg1 entity
---@param arg2? any
---@return animation_component?
function registry.add_animation(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return audio_listener_component?
function registry.add_audio_listener(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return audio_source_component?
function registry.add_audio_source(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return box_collider_component?
function registry.add_box_collider(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return camera_follow_component?
function registry.add_camera_follow(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return capsule_primitive_component?
function registry.add_capsule(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return capsule_collider_component?
function registry.add_capsule_collider(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return chunk_resident_component?
function registry.add_chunk_resident(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return circle_primitive_component?
function registry.add_circle(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return circle_collider_component?
function registry.add_circle_collider(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return color_grid_component?
function registry.add_color_grid(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return lifetime_component?
function registry.add_lifetime(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return line_primitive_component?
function registry.add_line(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return line_collider_component?
function registry.add_line_collider(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return name_component?
function registry.add_name(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return pivot_component?
function registry.add_pivot(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return position_component?
function registry.add_position(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return rigidbody_component?
function registry.add_rigidbody(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return rotation_component?
function registry.add_rotation(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return scale_component?
function registry.add_scale(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return script_component?
function registry.add_script(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return script_callback_component?
function registry.add_script_callbacks(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return sprite_component?
function registry.add_sprite(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return square_primitive_component?
function registry.add_square(arg1, arg2) end

---@param arg1 entity
---@param arg2 string
function registry.add_tag(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return text_label_component?
function registry.add_text_label(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return triangle_primitive_component?
function registry.add_triangle(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return triangle_collider_component?
function registry.add_triangle_collider(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_anchor_component?
function registry.add_ui_anchor(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_button_component?
function registry.add_ui_button(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_canvas_component?
function registry.add_ui_canvas(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_clip_component?
function registry.add_ui_clip(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_flex_component?
function registry.add_ui_flex(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_focus_component?
function registry.add_ui_focus(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_layout_box_component?
function registry.add_ui_layout_box(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_nine_slice_component?
function registry.add_ui_nine_slice(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_rect_component?
function registry.add_ui_rect(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_scroll_component?
function registry.add_ui_scroll(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_style_state_component?
function registry.add_ui_style_state(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
---@return ui_z_index_component?
function registry.add_ui_z_index(arg1, arg2) end

---@param arg1 entity
---@return animation_component
function registry.get_animation(arg1) end

---@param arg1 entity
---@return audio_listener_component
function registry.get_audio_listener(arg1) end

---@param arg1 entity
---@return audio_source_component
function registry.get_audio_source(arg1) end

---@param arg1 entity
---@return box_collider_component
function registry.get_box_collider(arg1) end

---@param arg1 entity
---@return camera_follow_component
function registry.get_camera_follow(arg1) end

---@param arg1 entity
---@return capsule_primitive_component
function registry.get_capsule(arg1) end

---@param arg1 entity
---@return capsule_collider_component
function registry.get_capsule_collider(arg1) end

---@param arg1 entity
---@return chunk_resident_component
function registry.get_chunk_resident(arg1) end

---@param arg1 entity
---@return circle_primitive_component
function registry.get_circle(arg1) end

---@param arg1 entity
---@return circle_collider_component
function registry.get_circle_collider(arg1) end

---@param arg1 entity
---@return color_grid_component
function registry.get_color_grid(arg1) end

---@param arg1 entity
---@return lifetime_component
function registry.get_lifetime(arg1) end

---@param arg1 entity
---@return line_primitive_component
function registry.get_line(arg1) end

---@param arg1 entity
---@return line_collider_component
function registry.get_line_collider(arg1) end

---@param arg1 entity
---@return name_component
function registry.get_name(arg1) end

---@param arg1 entity
---@return any
function registry.get_parent(arg1) end

---@param arg1 entity
---@return pivot_component
function registry.get_pivot(arg1) end

---@param arg1 entity
---@return position_component
function registry.get_position(arg1) end

---@param arg1 entity
---@return rigidbody_component
function registry.get_rigidbody(arg1) end

---@param arg1 entity
---@return rotation_component
function registry.get_rotation(arg1) end

---@param arg1 entity
---@return scale_component
function registry.get_scale(arg1) end

---@param arg1 entity
---@return script_component
function registry.get_script(arg1) end

---@param arg1 entity
---@return script_callback_component
function registry.get_script_callbacks(arg1) end

---@param arg1 entity
---@return sprite_component
function registry.get_sprite(arg1) end

---@param arg1 entity
---@return square_primitive_component
function registry.get_square(arg1) end

---@param arg1 entity
---@return text_label_component
function registry.get_text_label(arg1) end

---@param arg1 entity
---@return triangle_primitive_component
function registry.get_triangle(arg1) end

---@param arg1 entity
---@return triangle_collider_component
function registry.get_triangle_collider(arg1) end

---@param arg1 entity
---@return ui_anchor_component
function registry.get_ui_anchor(arg1) end

---@param arg1 entity
---@return ui_button_component
function registry.get_ui_button(arg1) end

---@param arg1 entity
---@return ui_canvas_component
function registry.get_ui_canvas(arg1) end

---@param arg1 entity
---@return ui_clip_component
function registry.get_ui_clip(arg1) end

---@param arg1 entity
---@return ui_flex_component
function registry.get_ui_flex(arg1) end

---@param arg1 entity
---@return ui_focus_component
function registry.get_ui_focus(arg1) end

---@param arg1 entity
---@return ui_layout_box_component
function registry.get_ui_layout_box(arg1) end

---@param arg1 entity
---@return ui_nine_slice_component
function registry.get_ui_nine_slice(arg1) end

---@param arg1 entity
---@return ui_rect_component
function registry.get_ui_rect(arg1) end

---@param arg1 entity
---@return ui_scroll_component
function registry.get_ui_scroll(arg1) end

---@param arg1 entity
---@return ui_style_state_component
function registry.get_ui_style_state(arg1) end

---@param arg1 entity
---@return ui_z_index_component
function registry.get_ui_z_index(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_animation(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_audio_listener(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_audio_source(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_box_collider(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_camera_follow(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_capsule(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_capsule_collider(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_chunk_resident(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_circle(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_circle_collider(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_color_grid(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_lifetime(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_line(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_line_collider(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_name(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_pivot(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_position(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_rigidbody(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_rotation(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_scale(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_script(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_script_callbacks(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_sprite(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_square(arg1) end

---@param arg1 entity
---@param arg2 string
---@return boolean
function registry.has_tag(arg1, arg2) end

---@param arg1 entity
---@return boolean
function registry.has_text_label(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_triangle(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_triangle_collider(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_anchor(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_button(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_canvas(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_clip(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_flex(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_focus(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_layout_box(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_nine_slice(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_rect(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_scroll(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_style_state(arg1) end

---@param arg1 entity
---@return boolean
function registry.has_ui_z_index(arg1) end

---@overload fun(): boolean
---@param arg1 any
---@return boolean
function registry.is_alive(arg1) end

---@return boolean
function registry.is_paused() end

---@param arg1? entity
---@return boolean
function registry.remove_animation(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_audio_listener(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_audio_source(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_box_collider(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_camera_follow(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_capsule(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_capsule_collider(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_chunk_resident(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_circle(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_circle_collider(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_color_grid(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_lifetime(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_line(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_line_collider(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_name(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_pivot(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_position(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_rigidbody(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_rotation(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_scale(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_script(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_script_callbacks(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_sprite(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_square(arg1) end

---@param arg1 entity
---@param arg2 string
function registry.remove_tag(arg1, arg2) end

---@param arg1? entity
---@return boolean
function registry.remove_text_label(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_triangle(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_triangle_collider(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_anchor(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_button(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_canvas(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_clip(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_flex(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_focus(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_layout_box(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_nine_slice(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_rect(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_scroll(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_style_state(arg1) end

---@param arg1? entity
---@return boolean
function registry.remove_ui_z_index(arg1) end

---@param arg1 entity
---@param arg2 entity
function registry.set_parent(arg1, arg2) end

---@param arg1 boolean
function registry.set_paused(arg1) end

---@param arg1? entity
---@return animation_component?
function registry.try_get_animation(arg1) end

---@param arg1? entity
---@return audio_listener_component?
function registry.try_get_audio_listener(arg1) end

---@param arg1? entity
---@return audio_source_component?
function registry.try_get_audio_source(arg1) end

---@param arg1? entity
---@return box_collider_component?
function registry.try_get_box_collider(arg1) end

---@param arg1? entity
---@return camera_follow_component?
function registry.try_get_camera_follow(arg1) end

---@param arg1? entity
---@return capsule_primitive_component?
function registry.try_get_capsule(arg1) end

---@param arg1? entity
---@return capsule_collider_component?
function registry.try_get_capsule_collider(arg1) end

---@param arg1? entity
---@return chunk_resident_component?
function registry.try_get_chunk_resident(arg1) end

---@param arg1? entity
---@return circle_primitive_component?
function registry.try_get_circle(arg1) end

---@param arg1? entity
---@return circle_collider_component?
function registry.try_get_circle_collider(arg1) end

---@param arg1? entity
---@return color_grid_component?
function registry.try_get_color_grid(arg1) end

---@param arg1? entity
---@return lifetime_component?
function registry.try_get_lifetime(arg1) end

---@param arg1? entity
---@return line_primitive_component?
function registry.try_get_line(arg1) end

---@param arg1? entity
---@return line_collider_component?
function registry.try_get_line_collider(arg1) end

---@param arg1? entity
---@return name_component?
function registry.try_get_name(arg1) end

---@param arg1? entity
---@return pivot_component?
function registry.try_get_pivot(arg1) end

---@param arg1? entity
---@return position_component?
function registry.try_get_position(arg1) end

---@param arg1? entity
---@return rigidbody_component?
function registry.try_get_rigidbody(arg1) end

---@param arg1? entity
---@return rotation_component?
function registry.try_get_rotation(arg1) end

---@param arg1? entity
---@return scale_component?
function registry.try_get_scale(arg1) end

---@param arg1? entity
---@return script_component?
function registry.try_get_script(arg1) end

---@param arg1? entity
---@return script_callback_component?
function registry.try_get_script_callbacks(arg1) end

---@param arg1? entity
---@return sprite_component?
function registry.try_get_sprite(arg1) end

---@param arg1? entity
---@return square_primitive_component?
function registry.try_get_square(arg1) end

---@param arg1? entity
---@return text_label_component?
function registry.try_get_text_label(arg1) end

---@param arg1? entity
---@return triangle_primitive_component?
function registry.try_get_triangle(arg1) end

---@param arg1? entity
---@return triangle_collider_component?
function registry.try_get_triangle_collider(arg1) end

---@param arg1? entity
---@return ui_anchor_component?
function registry.try_get_ui_anchor(arg1) end

---@param arg1? entity
---@return ui_button_component?
function registry.try_get_ui_button(arg1) end

---@param arg1? entity
---@return ui_canvas_component?
function registry.try_get_ui_canvas(arg1) end

---@param arg1? entity
---@return ui_clip_component?
function registry.try_get_ui_clip(arg1) end

---@param arg1? entity
---@return ui_flex_component?
function registry.try_get_ui_flex(arg1) end

---@param arg1? entity
---@return ui_focus_component?
function registry.try_get_ui_focus(arg1) end

---@param arg1? entity
---@return ui_layout_box_component?
function registry.try_get_ui_layout_box(arg1) end

---@param arg1? entity
---@return ui_nine_slice_component?
function registry.try_get_ui_nine_slice(arg1) end

---@param arg1? entity
---@return ui_rect_component?
function registry.try_get_ui_rect(arg1) end

---@param arg1? entity
---@return ui_scroll_component?
function registry.try_get_ui_scroll(arg1) end

---@param arg1? entity
---@return ui_style_state_component?
function registry.try_get_ui_style_state(arg1) end

---@param arg1? entity
---@return ui_z_index_component?
function registry.try_get_ui_z_index(arg1) end


---@return boolean
function reload_scene() end

---@param arg1 entity
---@param arg2 string
function remove_tag(arg1, arg2) end

---@class rigidbody_component
---@field velocity vec2
rigidbody_component = {}

---@param ... any
---@return rigidbody_component
function rigidbody_component.new(...) end

---@class rotation_component
---@field value number
rotation_component = {}

---@param ... any
---@return rotation_component
function rotation_component.new(...) end

---@return boolean
function save_user_settings() end

---@class scale_component
---@field scale number
---@field value vec2
---@field x number
---@field y number
scale_component = {}

---@param ... any
---@return scale_component
function scale_component.new(...) end

---@return boolean
function scale_component:is_uniform() end

---@class script_callback_component
---@field on_death any
---@field on_hit any
---@field script_table any
script_callback_component = {}

---@param ... any
---@return script_callback_component
function script_callback_component.new(...) end

---@class script_component
---@field data table
script_component = {}

---@param ... any
---@return script_component
function script_component.new(...) end

---@param arg1 string
---@param arg2 integer
---@param arg3? number
function set_audio_clip_limits(arg1, arg2, arg3) end

---@param arg1 string
---@param arg2 number
function set_audio_cue_gain(arg1, arg2) end

---@param arg1 boolean
function set_audio_muted(arg1) end

---@param arg1 number
function set_camera_zoom(arg1) end

---@param arg1 string
---@return boolean
function set_clipboard_text(arg1) end

---@param arg1 boolean
function set_debug_gui(arg1) end

---@param arg1 boolean
function set_deterministic(arg1) end

---@param arg1 number
function set_fixed_dt(arg1) end

---@param arg1 integer
function set_fps_target(arg1) end

---@param arg1 boolean
function set_fullscreen(arg1) end

---@param arg1 integer
---@param arg2 integer
---@param arg3? number
---@return boolean
function set_fullscreen_resolution(arg1, arg2, arg3) end

---@param arg1 number
---@param arg2 number
function set_game_map_dimensions(arg1, arg2) end

---@param arg1 boolean
function set_headless(arg1) end

---@param arg1 boolean
---@return boolean
function set_hot_reload_enabled(arg1) end

---@param arg1 number
function set_master_volume(arg1) end

---@param arg1 boolean
function set_maximized(arg1) end

---@param arg1 entity
---@param arg2 string
function set_name(arg1, arg2) end

---@param arg1 number
---@param arg2? boolean
function set_native_sfx_volume(arg1, arg2) end

---@param arg1 entity
---@param arg2 number
---@param arg3 number
function set_position(arg1, arg2, arg3) end

---@param arg1 string
---@return boolean
function set_presentation_mode(arg1) end

---@param arg1 entity
---@param arg2 number
function set_rotation(arg1, arg2) end

---@param arg1 number
function set_sim_seed(arg1) end

---@param arg1 integer
function set_sim_substeps(arg1) end

---@param arg1 entity
---@param arg2 string
---@param arg3 number
---@param arg4 number
---@param arg5 number
---@param arg6 number
function set_sprite_region(arg1, arg2, arg3, arg4, arg5, arg6) end

---@param arg1 entity
---@param arg2 number
---@param arg3 number
function set_sprite_src_rect(arg1, arg2, arg3) end

---@param arg1 entity
---@param arg2 number
---@param arg3 number
function set_sprite_world_size(arg1, arg2, arg3) end

---@param arg1 entity
---@param arg2 string
function set_text_label_font(arg1, arg2) end

---@param arg1 string
---@return boolean
function set_texture_scale_mode(arg1) end

---@param arg1 entity
---@param arg2 number
---@param arg3 number
---@param arg4 number
---@param arg5 number
function set_ui_nine_slice(arg1, arg2, arg3, arg4, arg5) end

---@param arg1 string
---@param arg2 any
---@return boolean
function set_user_setting(arg1, arg2) end

---@param arg1 boolean
---@return boolean
function set_vsync(arg1) end

---@param arg1 integer
---@return boolean
function set_window_display(arg1) end

---@param arg1 string
---@return boolean
function set_window_mode(arg1) end

---@param arg1 integer
---@param arg2 integer
function set_window_position(arg1, arg2) end

---@param arg1 boolean
function set_window_resizable(arg1) end

---@param arg1 integer
---@param arg2 integer
function set_window_size(arg1, arg2) end

---@class settings
settings = {}

---@param arg1 any
---@param arg2 any
---@return boolean
function settings.define(arg1, arg2) end

---@param arg1 string
---@param arg2 any
---@return any
function settings.get(arg1, arg2) end

---@return table
function settings.get_all() end

---@param arg1 string
---@return any
function settings.get_default(arg1) end

---@param arg1 string
---@return boolean
function settings.has(arg1) end

---@param arg1 string
---@return any
function settings.reset(arg1) end

function settings.reset_all() end

---@return boolean
function settings.save() end

---@param arg1 string
---@param arg2 any
---@return boolean
function settings.set(arg1, arg2) end


---@class sprite_component
---@field alpha integer
---@field asset_id string
---@field blend_mode string
---@field color_mod color
---@field height number
---@field layer integer
---@field width number
sprite_component = {}

---@param ... any
---@return sprite_component
function sprite_component.new(...) end

---@class square_primitive_component
---@field blend_mode string
---@field border_color color
---@field border_thickness number
---@field color color
---@field corner_radius number
---@field fill_ratio number
---@field height number
---@field is_fixed boolean
---@field layer integer
---@field position vec2
---@field track_color color
---@field width number
square_primitive_component = {}

---@param ... any
---@return square_primitive_component
function square_primitive_component.new(...) end

function stop_scene() end

---@class storage
storage = {}

---@param arg1 string
---@return boolean
function storage.exists(arg1) end

---@param arg1? string
---@return table
function storage.list(arg1) end

---@param arg1 string
---@return string?
function storage.read(arg1) end

---@param arg1 string
---@return table?
function storage.read_table(arg1) end

---@param arg1 string
---@return boolean
function storage.remove(arg1) end

---@param arg1 string
---@param arg2 string
---@return boolean
function storage.write(arg1, arg2) end

---@param arg1 string
---@param arg2 table
---@return boolean
function storage.write_table(arg1, arg2) end


---@class text_label_component
---@field color color
---@field font_id string
---@field h_align integer
---@field is_fixed boolean
---@field layer integer
---@field outline_color color
---@field outline_thickness number
---@field outline_width number
---@field position vec2
---@field shadow_color color
---@field shadow_offset vec2
---@field text string
---@field v_align integer
---@field wrap boolean
---@field wrap_width integer
text_label_component = {}

---@param ... any
---@return text_label_component
function text_label_component.new(...) end

---@param arg1 color|number
---@param arg2? number
function text_label_component:set_outline(arg1, arg2) end

---@class tilemap
---@field MASK_1 integer
---@field MASK_2 integer
---@field MASK_3 integer
---@field MASK_4 integer
---@field MASK_5 integer
---@field MASK_6 integer
---@field MASK_7 integer
---@field MASK_CHASM integer
---@field MASK_EMPTY integer
---@field MASK_GROUND_BLOCKED integer
---@field MASK_HAZARD integer
---@field MASK_PROJECTILE_BLOCKED integer
---@field MASK_SOLID integer
---@field MASK_SURFACE integer
---@field MASK_WALL integer
---@field TILE_1 integer
---@field TILE_2 integer
---@field TILE_3 integer
---@field TILE_4 integer
---@field TILE_5 integer
---@field TILE_6 integer
---@field TILE_7 integer
---@field TILE_CHASM integer
---@field TILE_EMPTY integer
---@field TILE_HAZARD integer
---@field TILE_SOLID integer
---@field TILE_SURFACE integer
---@field TILE_WALL integer
tilemap = {}

function tilemap.clear_chunk_overrides() end

function tilemap.clear_consumed_props() end

---@return integer
function tilemap.get_active_chunk_count() end

---@return integer, integer
function tilemap.get_camera_chunk() end

---@return integer, integer, integer, integer, integer
function tilemap.get_chunk_size() end

---@param arg1 integer
---@param arg2 integer
---@return string
function tilemap.get_chunk_template_id(arg1, arg2) end

---@return integer, integer
function tilemap.get_default_masks() end

---@return integer
function tilemap.get_seed() end

---@return string
function tilemap.get_stitch_mode() end

---@return table
function tilemap.get_templates() end

---@param arg1 number
---@param arg2 number
---@return integer
function tilemap.get_tile_at(arg1, arg2) end

---@return boolean
function tilemap.is_loaded() end

---@param arg1 integer
---@param arg2 integer
---@param arg3 integer
---@return boolean
function tilemap.is_prop_consumed(arg1, arg2, arg3) end

---@param arg1 number
---@param arg2 number
---@param arg3? integer
---@return boolean
function tilemap.is_solid_at(arg1, arg2, arg3) end

---@param arg1 string
---@param arg2? table
---@return boolean
function tilemap.load(arg1, arg2) end

---@param arg1 table
---@param arg2? table
---@return boolean
function tilemap.load_data(arg1, arg2) end

---@param arg1 string
---@param arg2? table
---@return boolean
function tilemap.load_json(arg1, arg2) end

---@param arg1 integer
---@param arg2 integer
---@param arg3 integer
function tilemap.mark_prop_consumed(arg1, arg2, arg3) end

---@param arg1 number
---@param arg2 number
---@param arg3 number
---@param arg4 number
---@param arg5? integer
---@return boolean, number, number, number, number, number, integer
function tilemap.raycast(arg1, arg2, arg3, arg4, arg5) end

---@return boolean
function tilemap.reload() end

---@param arg1 number
---@param arg2 number
---@param arg3 number
---@param arg4 number
---@param arg5? integer
---@return number, number
function tilemap.resolve_aabb(arg1, arg2, arg3, arg4, arg5) end

---@param arg1 number
---@param arg2 number
---@param arg3 number
---@param arg4? integer
---@return number, number
function tilemap.resolve_circle(arg1, arg2, arg3, arg4) end

---@param arg1 integer
---@param arg2 integer
---@param arg3? string
---@return boolean
function tilemap.set_chunk_override(arg1, arg2, arg3) end

---@param arg1 boolean
---@param arg2 boolean
function tilemap.set_debug_overlay(arg1, arg2) end

---@param arg1 integer
---@param arg2 integer
function tilemap.set_default_masks(arg1, arg2) end

---@param arg1 any
function tilemap.set_prop_spawner(arg1) end

---@param arg1 string
---@param arg2? integer
function tilemap.set_stitch_mode(arg1, arg2) end

---@param arg1 integer
---@param arg2 integer
function tilemap.set_window_radii(arg1, arg2) end

function tilemap.unload() end


---@return boolean
function toggle_debug_gui() end

---@class triangle_collider_component
---@field is_fixed boolean
---@field p1 vec2
---@field p2 vec2
---@field p3 vec2
triangle_collider_component = {}

---@param ... any
---@return triangle_collider_component
function triangle_collider_component.new(...) end

---@class triangle_primitive_component
---@field blend_mode string
---@field color color
---@field filled boolean
---@field is_fixed boolean
---@field layer integer
---@field p1 vec2
---@field p2 vec2
---@field p3 vec2
---@field thickness number
triangle_primitive_component = {}

---@param ... any
---@return triangle_primitive_component
function triangle_primitive_component.new(...) end

---@class ui
ui = {}

---@param arg1 entity
---@param arg2 any
---@param arg3? table
function ui.anchor(arg1, arg2, arg3) end

---@param arg1 entity
---@param arg2? table
function ui.box(arg1, arg2) end

---@param arg1 entity
---@param arg2? table
function ui.canvas(arg1, arg2) end

---@param arg1 entity
---@param arg2? any
function ui.clip(arg1, arg2) end

---@param arg1? boolean
---@return boolean
function ui.debug_draw(arg1) end

---@param arg1 entity
---@param arg2? table
function ui.flex(arg1, arg2) end

---@param arg1? entity
function ui.focus(arg1) end

---@return number
function ui.get_auto_scale() end

---@return entity?
function ui.get_focused() end

---@return number
function ui.get_scale() end

---@param arg1 any
---@param arg2? table
---@return any
function ui.mount(arg1, arg2) end

---@param arg1 entity
---@param arg2 any
function ui.nine_slice(arg1, arg2) end

---@param arg1? table
---@return table
function ui.observable(arg1) end

---@param arg1 string
---@param arg2 table
function ui.register_widget(arg1, arg2) end

---@param arg1 entity
---@param arg2? table
function ui.scroll(arg1, arg2) end

---@param arg1? number
---@return number
function ui.set_scale(arg1) end

---@param arg1 entity
---@param arg2 integer
function ui.z_index(arg1, arg2) end


---@class ui_anchor_component
---@field anchor_bottom number
---@field anchor_left number
---@field anchor_right number
---@field anchor_top number
---@field offset_bottom number
---@field offset_left number
---@field offset_right number
---@field offset_top number
ui_anchor_component = {}

---@param ... any
---@return ui_anchor_component
function ui_anchor_component.new(...) end

---@class ui_button_component
---@field action string
---@field click_sound string
---@field controller_button string
---@field focus_sound string
---@field gamepad string
---@field gamepad_button string
---@field has_hover_color boolean
---@field has_press_color boolean
---@field hover_color color
---@field hover_sound string
---@field input_key string
---@field is_active boolean
---@field is_fixed boolean
---@field is_hovered boolean
---@field is_pressed boolean
---@field key string
---@field on_click fun(...)
---@field on_focus fun(...)
---@field press_color color
ui_button_component = {}

---@param ... any
---@return ui_button_component
function ui_button_component.new(...) end

---@param arg1? entity
---@return boolean
function ui_button_component:trigger(arg1) end

---@class ui_canvas_component
---@field base_layer integer
---@field height number
---@field is_fixed boolean
---@field width number
ui_canvas_component = {}

---@param ... any
---@return ui_canvas_component
function ui_canvas_component.new(...) end

---@class ui_clip_component
---@field enabled boolean
ui_clip_component = {}

---@param ... any
---@return ui_clip_component
function ui_clip_component.new(...) end

---@class ui_flex_component
---@field columns integer
---@field cross_gap number
---@field direction string
---@field gap number
---@field padding_bottom number
---@field padding_left number
---@field padding_right number
---@field padding_top number
---@field wrap boolean
ui_flex_component = {}

---@param ... any
---@return ui_flex_component
function ui_flex_component.new(...) end

---@class ui_focus_component
---@field checked boolean
---@field disabled boolean
---@field focusable boolean
---@field inert boolean
---@field is_focused boolean
---@field nav_down entity
---@field nav_left entity
---@field nav_right entity
---@field nav_up entity
---@field tab_index integer
ui_focus_component = {}

---@param ... any
---@return ui_focus_component
function ui_focus_component.new(...) end

---@class ui_layout_box_component
---@field height number
---@field margin_bottom number
---@field margin_left number
---@field margin_right number
---@field margin_top number
---@field max_height number
---@field max_width number
---@field min_height number
---@field min_width number
---@field visible boolean
---@field width number
ui_layout_box_component = {}

---@param ... any
---@return ui_layout_box_component
function ui_layout_box_component.new(...) end

---@class ui_nine_slice_component
---@field bottom number
---@field left number
---@field right number
---@field top number
ui_nine_slice_component = {}

---@param ... any
---@return ui_nine_slice_component
function ui_nine_slice_component.new(...) end

---@class ui_rect_component
---@field blocks_pointer boolean
---@field bottom number
---@field center vec2
---@field height number
---@field layer integer
---@field left number
---@field right number
---@field top number
---@field width number
ui_rect_component = {}

---@param ... any
---@return ui_rect_component
function ui_rect_component.new(...) end

---@class ui_scroll_component
---@field content_height number
---@field content_width number
---@field horizontal boolean
---@field scroll_x number
---@field scroll_y number
---@field vertical boolean
---@field wheel_step number
ui_scroll_component = {}

---@param ... any
---@return ui_scroll_component
function ui_scroll_component.new(...) end

---@class ui_style_patch
---@field bg_color color
---@field border_color color
---@field border_width integer
---@field flags integer
---@field opacity integer
---@field text_color color
ui_style_patch = {}

---@param ... any
---@return ui_style_patch
function ui_style_patch.new(...) end

---@class ui_style_state_component
---@field checked ui_style_patch
---@field checked_hover ui_style_patch
---@field checked_press ui_style_patch
---@field disabled ui_style_patch
---@field focus ui_style_patch
---@field hover ui_style_patch
---@field press ui_style_patch
ui_style_state_component = {}

---@param ... any
---@return ui_style_state_component
function ui_style_state_component.new(...) end

---@class ui_z_index_component
---@field z integer
ui_z_index_component = {}

---@param ... any
---@return ui_z_index_component
function ui_z_index_component.new(...) end

---@class vec2
---@field x number
---@field y number
vec2 = {}

---@param ... any
---@return vec2
function vec2.new(...) end

---@param arg1 string
---@param arg2 string
---@return boolean
function write_project_file(arg1, arg2) end

---@param arg1 string
---@return boolean
function write_state_dump(arg1) end

