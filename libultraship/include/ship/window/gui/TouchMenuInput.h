#pragma once

// Touch handling for ImGui menus on iPhone and iPad.
//
// ImGui is built for a mouse, so on a touch screen a finger that lands on a
// slider or combo grabs it instead of scrolling the list, and scrolling only
// works by dragging the thin scrollbar. This turns one-finger touches into the
// gestures people expect from a phone:
//
//   tap            - presses the control under the finger
//   drag           - scrolls the panel under the finger, with momentum
//   sideways drag  - moves a slider, unless that panel scrolls sideways
//   press and hold - grabs the control (fine slider adjustment)
//
// It only acts while the menu (or a popup) is open, so in-game touch controls
// are untouched.

#if defined(__IOS__) && !defined(__TVOS__)

union SDL_Event;

namespace Ship::TouchMenuInput {

// Returns true when the event was handled here and must not reach the ImGui
// SDL backend. Call before ImGui_ImplSDL2_ProcessEvent.
bool HandleSdlEvent(const SDL_Event* event, bool menuOpen);

// Call between the backend NewFrame and ImGui::NewFrame (long-press, momentum).
void BeforeNewFrame();

// Call right after ImGui::NewFrame (applies the scroll to the touched panel).
void AfterNewFrame();

// Two or more fingers are down: drop any one-finger gesture in progress.
void Cancel();

} // namespace Ship::TouchMenuInput

#endif
