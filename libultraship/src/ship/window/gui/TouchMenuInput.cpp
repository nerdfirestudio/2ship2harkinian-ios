#include "ship/window/gui/TouchMenuInput.h"

#if defined(__IOS__) && !defined(__TVOS__)

#include <cfloat>
#include <cmath>
#include <SDL_events.h>
#include <SDL_timer.h>
#include <SDL_touch.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace Ship::TouchMenuInput {
namespace {

// Distances are in ImGui units (points). 10pt is about the wobble of a
// resting fingertip; anything past it is a deliberate drag.
constexpr float kTouchSlop = 10.0f;
// A finger held still this long grabs the control instead of waiting for a tap.
constexpr Uint64 kLongPressMs = 350;
// Momentum: flick speed decays by this factor per second; stop below kMinSpeed.
constexpr float kFriction = 4.5f;
constexpr float kMinSpeed = 30.0f;

enum class State { Idle, Pending, Scrolling, Pressed };

State sState = State::Idle;
ImVec2 sDownPos;
ImVec2 sLastPos;
Uint64 sDownTime = 0;
Uint64 sLastMoveTime = 0;
Uint64 sLastFrameTime = 0;
ImVec2 sVelocity;        // points per second, finger direction
ImVec2 sPendingScroll;   // points to add to the scroll offset this frame
ImGuiID sScrollTargetX = 0;
ImGuiID sScrollTargetY = 0;
bool sMomentum = false;

ImGuiWindow* WindowUnder(const ImVec2& pos) {
    if (GImGui == nullptr) {
        return nullptr;
    }
    ImGuiWindow* hovered = nullptr;
    ImGuiWindow* underMoving = nullptr;
    ImGui::FindHoveredWindowEx(pos, true, &hovered, &underMoving);
    return hovered;
}

// The innermost window at the finger that can scroll along an axis.
ImGuiWindow* ScrollableAt(ImGuiWindow* window, int axis) {
    for (ImGuiWindow* w = window; w != nullptr; w = w->ParentWindow) {
        if (w->Flags & ImGuiWindowFlags_NoScrollWithMouse) {
            continue;
        }
        if (w->ScrollMax[axis] > 0.0f) {
            return w;
        }
    }
    return nullptr;
}

void ParkMouse() {
    // A finger that lifted is not a hovering mouse: clear hover highlights and tooltips.
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void Grab(const ImVec2& at) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(sDownPos.x, sDownPos.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    if (at.x != sDownPos.x || at.y != sDownPos.y) {
        io.AddMousePosEvent(at.x, at.y);
    }
    sState = State::Pressed;
}

} // namespace

void Cancel() {
    if (sState == State::Pressed) {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    }
    sState = State::Idle;
    sMomentum = false;
    sVelocity = ImVec2(0, 0);
    sPendingScroll = ImVec2(0, 0);
}

bool HandleSdlEvent(const SDL_Event* e, bool menuOpen) {
    const bool touchMouse =
        (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) ? e->button.which == SDL_TOUCH_MOUSEID
        : e->type == SDL_MOUSEMOTION                                     ? e->motion.which == SDL_TOUCH_MOUSEID
                                                                         : false;
    if (!touchMouse) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const Uint64 now = SDL_GetTicks64();

    if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (e->button.button != SDL_BUTTON_LEFT || !menuOpen) {
            return false;
        }
        sState = State::Pending;
        sMomentum = false;
        sVelocity = ImVec2(0, 0);
        sDownPos = sLastPos = ImVec2((float)e->button.x, (float)e->button.y);
        sDownTime = sLastMoveTime = now;
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(sDownPos.x, sDownPos.y);
        return true;
    }

    if (sState == State::Idle) {
        return false; // a touch that began outside the menu stays with the game
    }

    if (e->type == SDL_MOUSEMOTION) {
        const ImVec2 pos((float)e->motion.x, (float)e->motion.y);
        const ImVec2 step = pos - sLastPos;
        const float dt = (float)(now - sLastMoveTime) / 1000.0f;

        if (sState == State::Pending) {
            const ImVec2 moved = pos - sDownPos;
            if (ImLengthSqr(moved) >= kTouchSlop * kTouchSlop) {
                ImGuiWindow* under = WindowUnder(sDownPos);
                ImGuiWindow* scrollX = ScrollableAt(under, ImGuiAxis_X);
                ImGuiWindow* scrollY = ScrollableAt(under, ImGuiAxis_Y);
                const bool sideways = std::fabs(moved.x) > std::fabs(moved.y) * 1.2f;
                if (sideways && scrollX == nullptr) {
                    Grab(pos); // a sideways drag on a slider moves the slider
                } else {
                    sState = State::Scrolling;
                    sScrollTargetX = scrollX ? scrollX->ID : 0;
                    sScrollTargetY = scrollY ? scrollY->ID : 0;
                    // Include the slop so the content stays under the finger.
                    sPendingScroll -= moved;
                    ParkMouse();
                }
            }
        } else if (sState == State::Scrolling) {
            sPendingScroll -= step;
            if (dt > 0.0f) {
                const ImVec2 v = step / dt;
                sVelocity = sVelocity * 0.6f + v * 0.4f; // smooth out jittery samples
            }
        } else if (sState == State::Pressed) {
            io.AddMousePosEvent(pos.x, pos.y);
        }
        sLastPos = pos;
        sLastMoveTime = now;
        return true;
    }

    if (e->type == SDL_MOUSEBUTTONUP) {
        if (e->button.button != SDL_BUTTON_LEFT) {
            return true;
        }
        const ImVec2 pos((float)e->button.x, (float)e->button.y);
        if (sState == State::Pending) {
            // A tap: press and release where the finger landed. ImGui's input
            // queue spreads these over separate frames, so the click registers.
            io.AddMousePosEvent(sDownPos.x, sDownPos.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            ParkMouse();
        } else if (sState == State::Scrolling) {
            // A finger that stopped before lifting should not fling.
            const bool stillMoving = (now - sLastMoveTime) < 80;
            sMomentum = stillMoving && ImLengthSqr(sVelocity) > kMinSpeed * kMinSpeed;
            if (!sMomentum) {
                sVelocity = ImVec2(0, 0);
            }
        } else if (sState == State::Pressed) {
            io.AddMousePosEvent(pos.x, pos.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            ParkMouse();
        }
        sState = State::Idle;
        return true;
    }
    return true;
}

void BeforeNewFrame() {
    const Uint64 now = SDL_GetTicks64();
    const float dt = sLastFrameTime ? (float)(now - sLastFrameTime) / 1000.0f : 0.0f;
    sLastFrameTime = now;

    if (sState == State::Pending && now - sDownTime >= kLongPressMs) {
        Grab(sLastPos);
    }

    if (sMomentum && dt > 0.0f && dt < 0.25f) {
        sPendingScroll -= sVelocity * dt;
        sVelocity = sVelocity * std::exp(-kFriction * dt);
        if (ImLengthSqr(sVelocity) < kMinSpeed * kMinSpeed) {
            sMomentum = false;
            sVelocity = ImVec2(0, 0);
        }
    }
}

void AfterNewFrame() {
    if (sPendingScroll.x == 0.0f && sPendingScroll.y == 0.0f) {
        return;
    }
    bool moved = false;
    if (sScrollTargetY != 0 && sPendingScroll.y != 0.0f) {
        if (ImGuiWindow* w = ImGui::FindWindowByID(sScrollTargetY)) {
            const float target = ImClamp(w->Scroll.y + sPendingScroll.y, 0.0f, w->ScrollMax.y);
            moved |= target != w->Scroll.y;
            ImGui::SetScrollY(w, target);
        }
    }
    if (sScrollTargetX != 0 && sPendingScroll.x != 0.0f) {
        if (ImGuiWindow* w = ImGui::FindWindowByID(sScrollTargetX)) {
            const float target = ImClamp(w->Scroll.x + sPendingScroll.x, 0.0f, w->ScrollMax.x);
            moved |= target != w->Scroll.x;
            ImGui::SetScrollX(w, target);
        }
    }
    // Hitting the end of the list stops a fling instead of pushing against the edge.
    if (sMomentum && !moved) {
        sMomentum = false;
        sVelocity = ImVec2(0, 0);
    }
    sPendingScroll = ImVec2(0, 0);
}

} // namespace Ship::TouchMenuInput

#endif
