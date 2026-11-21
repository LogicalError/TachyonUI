#pragma once

#include <type_traits>

namespace tachyon
{
    /// @addtogroup app
    /// @{

    /// What a control shows and one view of it, as the control takes them. A struct that is
    /// both (TimelineView, CurveView, PlotView) passes as one; for several views of the same
    /// data, keep the data once and a view state per view, and pass `{ data, state }`.
    ///
    ///     TimelineData      animation;
    ///     TimelineViewState left, right;
    ///     TimelineEditor({ animation, left });
    ///     TimelineEditor({ animation, right });
    template<class Data, class State>
    struct BasicViewRef
    {
        Data&  data;
        State& state;

        BasicViewRef(Data& d, State& s) : data(d), state(s) {}
        template<class View> requires std::is_convertible_v<View&, Data&> && std::is_convertible_v<View&, State&>
        BasicViewRef(View& view) : data(view), state(view) {}
        template<class D, class S> requires std::is_convertible_v<D&, Data&> && std::is_convertible_v<S&, State&>
        BasicViewRef(const BasicViewRef<D, S>& other) : data(other.data), state(other.state) {}
    };

    /// @}
}
