#pragma once

#include <lvgl.h>
#include <stdint.h>

/** +1 = next page (finger left), -1 = previous page (finger right). */
using TouchRouterPageStepCb = void (*)(int8_t step);

void touchRouterBegin();
void touchRouterSetPageStepHandler(TouchRouterPageStepCb cb);

/**
 * Feed PRESSED / PRESSING / RELEASED / CLICKED / PRESS_LOST.
 * Returns true when the caller must ignore the event for button/tile actions.
 */
bool touchRouterHandleEvent(lv_event_t* e);

bool touchRouterAllowsClick();
bool touchRouterIsSwiping();
bool touchRouterSuppressesClick();
void touchRouterReset();
