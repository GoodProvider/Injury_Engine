#pragma once

// Whipping overlay: a PrismaUI view (Data/PrismaUI/views/Injury_Engine/overlay.html) with
// per-part damage/healing bars for loaded tracked actors. Never focused (no cursor, no input).
// C++ pushes InjuryEngine::OverlayJson() whenever it changes; the page decides which
// panels show (changed recently) and fades idle ones.
namespace Overlay
{
    // kDataLoaded: request the PrismaUI API and create the (hidden) view.
    void Init();

    // "Show whipping overlay" setting; off hides the view and stops pushes.
    void SetEnabled(bool enabled);

    // After engine state may have changed (hit, health sync, game-time healing). Any thread.
    void Notify();

    // New game / load: drop every panel.
    void Reset();
}
