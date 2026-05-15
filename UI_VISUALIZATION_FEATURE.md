# Signal Chain Visualizer (UI Visualization Feature)

The Signal Chain Visualizer is a high-signal "Visual Laboratory" located in the synth's title bar. It provides real-time, interactive feedback on how signal operations (mixing, subtractive filtering, and effects) affect the waveform.

## Architectural Design

The visualizer consists of three distinct panes, each representing a stage in the synth's signal path.

### 1. Source Pane (Idealized Math)
*   **Purpose:** Shows the "pure" starting point of the audio.
*   **Input:** Current parameters for Oscillator A (wave, PW), Oscillator B (wave, PW, detune), and Noise mix levels.
*   **Logic:** Calculates a single cycle (or two) of the combined oscillators mathematically on the UI thread.
*   **Visual Style:** A crisp, thin line in `textPrimary` white.

### 2. Filter Pane (Subtractive Preview)
*   **Purpose:** Visualizes the "subtractive" operation.
*   **Input:** The "Source" wave plus Filter Cutoff and Resonance.
*   **Logic:** Passes the calculated Source wave through a simulated low-pass filter (LPF) model. This is a mathematical simulation, not the actual audio DSP, allowing it to update even when no note is playing.
*   **Visual Style:** A colored line (e.g., `ledGreen`) showing the rounded and "resonated" version of the source wave.

### 3. Output Pane (Real-time Reality)
*   **Purpose:** Shows the "truth" of the final audio signal.
*   **Input:** Actual audio samples from the processor output.
*   **Logic:** Uses a lock-free circular buffer (FIFO) to capture samples from `processBlock`. A trigger mechanism (zero-crossing) ensures a stable oscilloscope-style display.
*   **Visual Style:** A dynamic, moving line in a distinct color (e.g., `learnYellow` or `badgeGreen`).

## UI Integration

*   **Location:** Title bar, immediately to the right of the CoolSynth logo.
*   **Space Allocation:** Approximately 500px width x 48px height.
*   **Layout:** Three equally sized panels (approx 160px each) with subtle borders and labels (Source, Filter, Output).

## Technical Implementation Details

*   **Component:** `SignalChainVisualizer` class inheriting from `juce::Component` and `juce::Timer`.
*   **DSP Sim:** A lightweight 1-pole or 2-pole simulation for the Filter pane to keep UI performance high.
*   **FIFO:** A lock-free circular buffer to bridge the Audio and UI threads for the Output pane.
*   **Optimization:** Repaint occurs via `timerCallback` (e.g., 30-60 fps) to avoid unnecessary work. "Source" and "Filter" panes only recalculate if their specific parameters change.
