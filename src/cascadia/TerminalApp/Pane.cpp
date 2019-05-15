// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "Pane.h"

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Microsoft::Terminal::Settings;
using namespace winrt::Microsoft::Terminal::TerminalControl;

static const int SEPERATOR_SIZE = 8;

Pane::Pane(GUID profile, winrt::Microsoft::Terminal::TerminalControl::TermControl control, const bool lastFocused) :
    _control{ control },
    _lastFocused{ lastFocused },
    _profile{ profile },
    _splitState{ SplitState::None },
    _firstChild{ nullptr },
    _secondChild{ nullptr },
    _connectionClosedToken{},
    _firstClosedToken{},
    _secondClosedToken{}
{
    _root = Controls::Grid{};
    _AddControlToRoot(_control);

    // Set the background of the pane to match that of the theme's default grid
    // background. This way, we'll match the small underline under the tabs, and
    // the UI will be consistent on bot light and dark modes.
    auto res = Application::Current().Resources();
    winrt::Windows::Foundation::IInspectable key = winrt::box_value(L"BackgroundGridThemeStyle");
    if (res.HasKey(key))
    {
        winrt::Windows::Foundation::IInspectable g = res.Lookup(key);
        winrt::Windows::UI::Xaml::Style style = g.try_as<winrt::Windows::UI::Xaml::Style>();
        _root.Style(style);
    }
}

Pane::~Pane()
{
}

// Method Description:
// - Adds a given Terminal Control to our root Grid. Also registers an event
//   handler to know when that control closed.
// Arguments:
// - control: The new TermControl to use as the content of our root Grid.
void Pane::_AddControlToRoot(TermControl control)
{
    _root.Children().Append(control.GetControl());

    _connectionClosedToken = control.ConnectionClosed([=]() {
        if (control.CloseOnExit())
        {
            // Fire our Closed event to tell our parent that we should be removed.
            _closedHandlers();
        }
    });
}

// Method Description:
// - Get the root UIElement of this pane. There may be a single TermControl as a
//   child, or an entire tree of grids and panes as children of this element.
// Return Value:
// - the Grid acting as the root of this pane.
Controls::Grid Pane::GetRootElement()
{
    return _root;
}

// Method Description:
// - Returns nullptr if no children of this pane were the last control to be
//   focused, or the TermControl that _was_ the last control to be focused (if
//   there was one).
// - This control might not currently be focused, if the tab itself is not
//   currently focused.
// Return Value:
// - nullptr if no children were marked `_lastFocused`, else the TermControl
//   that was last focused.
TermControl Pane::GetLastFocusedTerminalControl()
{
    if (_IsLeaf())
    {
        return _lastFocused ? _control : nullptr;
    }
    else
    {
        auto firstFocused = _firstChild->GetLastFocusedTerminalControl();
        if (firstFocused != nullptr)
        {
            return firstFocused;
        }
        auto secondFocused = _secondChild->GetLastFocusedTerminalControl();
        return secondFocused;
    }
}

// Method Description:
// - Returns nullopt if no children of this pane were the last control to be
//   focused, or the GUID of the profile of the last control to be focused (if
//   there was one).
// Return Value:
// - nullopt if no children of this pane were the last control to be
//   focused, else the GUID of the profile of the last control to be focused
std::optional<GUID> Pane::GetLastFocusedProfile() const noexcept
{
    if (_IsLeaf())
    {
        return _lastFocused ? _profile : std::nullopt;
    }
    else
    {
        auto firstFocused = _firstChild->GetLastFocusedProfile();
        if (firstFocused.has_value())
        {
            return firstFocused;
        }
        auto secondFocused = _secondChild->GetLastFocusedProfile();
        return secondFocused;
    }
}

// Method Description:
// - Returns true if this pane was the last pane to be focused in a tree of panes.
// Arguments:
// - <none>
// Return Value:
// - true iff we were the last pane focused in this tree of panes.
bool Pane::WasLastFocused() const noexcept
{
    return _lastFocused;
}

// Method Description:
// - Returns true iff this pane has no child panes.
// Arguments:
// - <none>
// Return Value:
// - true iff this pane has no child panes.
bool Pane::_IsLeaf() const noexcept
{
    return _splitState == SplitState::None;
}

// Method Description:
// - Returns true if this pane is currently focused, or there is a pane which is
//   a child of this pane that is actively focused
// Arguments:
// - <none>
// Return Value:
// - true if the currently focused pane is either this pane, or one of this
//   pane's descendants
bool Pane::_HasFocusedChild() const noexcept
{
    const bool controlFocused = _control != nullptr &&
                                _control.GetControl().FocusState() != FocusState::Unfocused;
    const bool firstFocused = _firstChild != nullptr && _firstChild->_HasFocusedChild();
    const bool secondFocused = _secondChild != nullptr && _secondChild->_HasFocusedChild();

    return controlFocused || firstFocused || secondFocused;
}

// Method Description:
// - Update the focus state of this pane, and all it's descendants.
//   * If this is a leaf node, and our control is actively focused, well mark
//     ourselves as the _lastFocused.
//   * If we're not a leaf, we'll recurse on our children to check them.
void Pane::CheckFocus()
{
    if (_IsLeaf())
    {
        const bool controlFocused = _control != nullptr &&
                                    _control.GetControl().FocusState() != FocusState::Unfocused;

        _lastFocused = controlFocused;
    }
    else
    {
        _lastFocused = false;
        _firstChild->CheckFocus();
        _secondChild->CheckFocus();
    }
}

// Method Description:
// - Attempts to update the settings of this pane or any children of this pane.
//   * If this pane is a leaf, and our profile guid matches the parameter, then
//     we'll apply the new settings to our control.
//   * If we're not a leaf, we'll recurse on our children.
// Arguments:
// - settings: The new TerminalSettings to apply to any matching controls
// - profile: The GUID of the profile these settings should apply to.
void Pane::CheckUpdateSettings(TerminalSettings settings, GUID profile)
{
    if (!_IsLeaf())
    {
        _firstChild->CheckUpdateSettings(settings, profile);
        _secondChild->CheckUpdateSettings(settings, profile);
    }
    else
    {
        if (profile == _profile)
        {
            _control.UpdateSettings(settings);
        }
    }
}

// Method Description:
// - Closes one of our children. In doing so, takes the control from the other
//   child, and makes this pane a leaf node again.
// Arguments:
// - closeFirst: if true, the first child should be closed, and the second
//   should be preserved, and vice-versa for false.
void Pane::_CloseChild(const bool closeFirst)
{
    auto remainingChild = closeFirst ? _secondChild : _firstChild;

    // If the only child left is a leaf, that means we're a leaf now.
    if (remainingChild->_IsLeaf())
    {
        // take the control and profile of the pane that _wasn't_ closed.
        _control = closeFirst ? _secondChild->_control : _firstChild->_control;
        _profile = closeFirst ? _secondChild->_profile : _firstChild->_profile;

        // If either of our children was focused, we want to take that focus from
        // them.
        _lastFocused = _firstChild->_lastFocused || _secondChild->_lastFocused;

        // Remove all the ui elements of our children. This'll make sure we can
        // re-attach the TermControl to our Grid.
        _firstChild->_root.Children().Clear();
        _secondChild->_root.Children().Clear();

        // Reset our UI:
        _root.Children().Clear();
        _root.ColumnDefinitions().Clear();
        _root.RowDefinitions().Clear();
        _separatorRoot = { nullptr };

        // Reattach the TermControl to our grid.
        _AddControlToRoot(_control);

        if (_lastFocused)
        {
            _control.GetControl().Focus(FocusState::Programmatic);
        }

        _splitState = SplitState::None;

        // Release our children.
        _firstChild = nullptr;
        _secondChild = nullptr;
    }
    else
    {
        // Revoke the old event handlers.
        _firstChild->Closed(_firstClosedToken);
        _secondChild->Closed(_secondClosedToken);

        // Steal all the state from our child
        _splitState = remainingChild->_splitState;
        _separatorRoot = remainingChild->_separatorRoot;
        _firstChild = remainingChild->_firstChild;
        _secondChild = remainingChild->_secondChild;

        // remainingChild->_root.Children().Clear();
        _root.Children().Clear();
        _root.ColumnDefinitions().Clear();
        _root.RowDefinitions().Clear();

        // Copy the UI over to our grid.
        auto oldCols = remainingChild->_root.ColumnDefinitions();
        auto oldRows = remainingChild->_root.RowDefinitions();

        // remainingChild->_root.Children().Clear();
        // remainingChild->_root.ColumnDefinitions().Clear();
        // remainingChild->_root.RowDefinitions().Clear();
        // TODO: These throw, because apparently the definitions are still
        // parented to another (the old) element. maybe we need to just
        // regenerate them?
        while (remainingChild->_root.ColumnDefinitions().Size() > 0)
        {
            auto col = remainingChild->_root.ColumnDefinitions().GetAt(0);
            remainingChild->_root.ColumnDefinitions().RemoveAt(0);
            _root.ColumnDefinitions().Append(col);
        }
        while (remainingChild->_root.RowDefinitions().Size() > 0)
        {
            auto row = remainingChild->_root.RowDefinitions().GetAt(0);
            remainingChild->_root.RowDefinitions().RemoveAt(0);
            _root.RowDefinitions().Append(row);
        }

        remainingChild->_root.Children().Clear();

        _root.Children().Append(_firstChild->GetRootElement());
        _root.Children().Append(_separatorRoot);
        _root.Children().Append(_secondChild->GetRootElement());


        _SetupChildCloseHandlers();

        remainingChild->_firstChild = nullptr;
        remainingChild->_secondChild = nullptr;
        remainingChild->_separatorRoot = { nullptr };

    }


}

// Method Description:
// - Adds event handlers to our chilcren to handle their close events.
void Pane::_SetupChildCloseHandlers()
{
    _firstClosedToken = _firstChild->Closed([this](){
        _root.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [=](){
            _CloseChild(true);
        });
    });

    _secondClosedToken = _secondChild->Closed([this](){
        _root.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, [=](){
            _CloseChild(false);
        });
    });
}

void Pane::SplitVertical(const GUID profile, TermControl control)
{
    // If we're not the leaf, recurse into our children to split them.
    if (!_IsLeaf())
    {
        if (_firstChild->_HasFocusedChild())
        {
            _firstChild->SplitVertical(profile, control);
        }
        else if (_secondChild->_HasFocusedChild())
        {
            _secondChild->SplitVertical(profile, control);
        }

        return;
    }

    // revoke our handler - the child will take care of the control now.
    _control.ConnectionClosed(_connectionClosedToken);

    _splitState = SplitState::Vertical;

    // Create three columns in this grid: one for each pane, and one for the separator.
    auto separatorColDef = Controls::ColumnDefinition();
    separatorColDef.Width(GridLengthHelper::Auto());

    _root.ColumnDefinitions().Append(Controls::ColumnDefinition{});
    _root.ColumnDefinitions().Append(separatorColDef);
    _root.ColumnDefinitions().Append(Controls::ColumnDefinition{});

    // Remove any children we currently have. We can't add the existing
    // TermControl to a new grid until we do this.
    _root.Children().Clear();

    // Create two new Panes
    //   Move our control, guid into the first one.
    //   Move the new guid, control into the second.
    _firstChild = std::make_shared<Pane>(_profile.value(), _control);
    _profile = std::nullopt;
    _control = { nullptr };
    _secondChild = std::make_shared<Pane>(profile, control);

    // add the first pane to row 0
    _root.Children().Append(_firstChild->GetRootElement());
    Controls::Grid::SetColumn(_firstChild->GetRootElement(), 0);

    // Create the pane separator, and place it in column 1
    _separatorRoot = Controls::Grid{};
    _separatorRoot.Width(SEPERATOR_SIZE);
    // NaN is the special value XAML uses for "Auto" sizing.
    _separatorRoot.Height(NAN);
    _root.Children().Append(_separatorRoot);
    Controls::Grid::SetColumn(_separatorRoot, 1);

    // add the second pane to column 2
    _root.Children().Append(_secondChild->GetRootElement());
    Controls::Grid::SetColumn(_secondChild->GetRootElement(), 2);

    // Register event handlers on our children to handle their Close events
    _SetupChildCloseHandlers();

    _lastFocused = false;
}

void Pane::SplitHorizontal(const GUID profile, winrt::Microsoft::Terminal::TerminalControl::TermControl control)
{
    if (!_IsLeaf())
    {
        if (_firstChild->_HasFocusedChild())
        {
            _firstChild->SplitHorizontal(profile, control);
        }
        else if (_secondChild->_HasFocusedChild())
        {
            _secondChild->SplitHorizontal(profile, control);
        }

        return;
    }

    // revoke our handler - the child will take care of the control now.
    _control.ConnectionClosed(_connectionClosedToken);

    _splitState = SplitState::Horizontal;

    // Create three rows in this grid: one for each pane, and one for the separator.
    auto separatorRowDef = Controls::RowDefinition();
    separatorRowDef.Height(GridLengthHelper::Auto());

    _root.RowDefinitions().Append(Controls::RowDefinition{});
    _root.RowDefinitions().Append(separatorRowDef);
    _root.RowDefinitions().Append(Controls::RowDefinition{});

    _root.Children().Clear();

    // Create two new Panes
    //   Move our control, guid into the first one.
    //   Move the new guid, control into the second.
    _firstChild = std::make_shared<Pane>(_profile.value(), _control);
    _profile = std::nullopt;
    _control = { nullptr };
    _secondChild = std::make_shared<Pane>(profile, control);

    // add the first pane to row 0
    _root.Children().Append(_firstChild->GetRootElement());
    Controls::Grid::SetRow(_firstChild->GetRootElement(), 0);

    _separatorRoot = Controls::Grid{};
    _separatorRoot.Height(SEPERATOR_SIZE);
    // NaN is the special value XAML uses for "Auto" sizing.
    _separatorRoot.Width(NAN);
    _root.Children().Append(_separatorRoot);
    Controls::Grid::SetRow(_separatorRoot, 1);

    // add the second pane to row 1
    _root.Children().Append(_secondChild->GetRootElement());
    Controls::Grid::SetRow(_secondChild->GetRootElement(), 2);

    _SetupChildCloseHandlers();

    _lastFocused = false;
}

DEFINE_EVENT(Pane, Closed, _closedHandlers, ConnectionClosedEventArgs);
