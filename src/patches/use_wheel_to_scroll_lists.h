//
// Created by Alex on 28.10.2026.
//

#ifndef FLAME_USE_WHEEL_TO_SCROLL_LISTS_H
#define FLAME_USE_WHEEL_TO_SCROLL_LISTS_H

namespace patch::use_wheel_to_scroll_lists {

    // wheelDelta: WHEEL_DELTA (120) per notch, positive away from the user.
    // returns true if the cursor is over a scrollable list, which then takes the wheel
    bool scroll_list_under_cursor(int wheelDelta);

}

#endif //FLAME_USE_WHEEL_TO_SCROLL_LISTS_H
