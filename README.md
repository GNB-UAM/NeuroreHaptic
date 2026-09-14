# NeuroreHaptic

Open-source experimental environment for the acquisition and analysis of haptic metrics in motor neurorehabilitation tasks.

This repository contains the C++ implementation of two interactive haptic tasks with different task predominance (physical and cognitive). It was developed as part of a Master's Thesis (TFM) as a research and proof-of-concept platform for studying motor performance and fatigue-related effects.

The system enables continuous data recording during task execution to analyze differences and response patterns under different fatigue conditions. **This software is intended for research purposes only and has not been clinically validated for diagnosis or therapy.**

## Tasks

* **`00-task1-p` - Physical-Predominance Task:** A continuous spatial maze with dynamic friction for analyzing isometric force control, contact with the surface, trajectory efficiency, and collisions with the maze walls.
* **`00-task1-c` - Cognitive-Predominance Haptic Stroop Task:** A haptic adaptation of the Stroop test for analyzing inhibitory control, response and decision times, movement, and errors.

## Experimental Design

The tasks can be used under different fatigue conditions according to the experimental protocol. Two task execution modes are available:

* **Normal:** standard task condition.
* **Crossed:** used when the task and the preceding fatigue condition differ.

The experimental conditions considered in the study are:

| Condition | Fatigue condition | Task      |
| --------- | ----------------- | --------- |
| **F-F**   | Physical          | Physical  |
| **F-C**   | Physical          | Cognitive |
| **C-C**   | Cognitive         | Cognitive |
| **C-F**   | Cognitive         | Physical  |

The repository provides the experimental tasks and data acquisition infrastructure. The classification of fatigue origin using machine-learning models is **not implemented** in the current version.

## Development Environment

* **Hardware:** Novint Falcon haptic device, with the corresponding device drivers installed.
* **Software:** CHAI3D v3.3.0 and Visual Studio 2022.
* **Operating System:** Windows.

## Technical Features

* **High-frequency data acquisition:** Kinematic and dynamic variables are recorded at 1000 Hz using a multithreading architecture that separates the haptic control loop from graphical rendering.
* **Structured data export:** Each repetition generates three CSV files containing raw time series, global summaries, and temporal event information.
* **Visual feedback:** The tasks provide visual feedback for conditions such as loss of contact and excessive applied force.
* **Configurable task parameters:** Physical and logical parameters are defined in the source code and can be adapted to other experimental protocols.
* **Subject and condition identification:** The system allows the experimenter to specify the subject identifier and recording condition.

## Main Parameters

Relevant parameters implemented in the current version include:

* **Physical task friction:** dynamic friction coefficient of `0.25`, corresponding to the calibrated "Medium" level used in the experiments.
* **Physical task contact threshold:** `0.1 N`.
* **Cognitive task contact threshold:** `0.05 N`.
* **Excessive-force threshold:** `10.5 N`, triggering the corresponding visual warning.
* **Stroop target zone:** radius of `0.05 u.v.`.
* **Stroop quadrant tolerance:** approximately `±0.035 u.v.`.
* **Stroop difficulty:** Normal mode uses 3 colors, while Hard mode uses 4 colors.
* **Number of cognitive trials:** configurable to **10, 20, or 30 words**.

These values correspond to the configuration used in the experimental protocol and can be modified in the source code.

## Data Output

For each repetition, the system automatically generates three CSV files containing raw time-series data, global summary metrics, and a temporal event log. These files are used for the subsequent analysis of task performance and error-related metrics.

## Installation and Execution

1. Download [CHAI3D v3.3.0](https://www.chai3d.org/download/releases).
2. Clone this repository inside the CHAI3D directory, specifically:
   `chai3d-3.3.0/mis_proyectos/`
3. Open the main CHAI3D Visual Studio 2022 solution:
   `chai3d-VS2022.sln`
4. Open and compile the `.cpp` files corresponding to the desired task.
5. Once compiled, the executables will be generated in the CHAI3D `bin` folder.
6. Run the corresponding `.exe` file from the `bin` folder.

## Configuring the Output Directory

> **Important:** The current source code contains absolute Windows paths corresponding to the original development environment used for the Master's Thesis.

Before running the tasks on another computer, search the corresponding `.cpp` file for:

```cpp
string baseDir =
```

and replace the existing path with a directory available on the local system.


## Data Privacy

The repository does not include experimental participant data. When reproducing the experiments, participant identifiers should be anonymized and the generated CSV files should be stored in an appropriate local or secured location.

## Repository Structure

```text
NeuroreHaptic/
├── 00-task1-p.cpp       # Physical-predominance task
├── 00-task1-c.cpp       # Cognitive-predominance haptic Stroop task
└── README.md
```

The repository can be placed inside the `mis_proyectos` directory of CHAI3D v3.3.0 as described above.

## Limitations

* The system currently depends on the **Novint Falcon** haptic device.
* The software is designed for **Windows and Visual Studio 2022**.
* The implementation is a **research/proof-of-concept platform** and is not a clinically validated medical or rehabilitation system.
* Task parameters are calibrated according to the experimental protocol and may require modification for other studies.
* The current repository does not implement the machine-learning classification of fatigue origin described as future work in the research.

## Reproducibility

The purpose of releasing the source code is to facilitate inspection, reproduction, and adaptation of the experimental environment.

Because the system depends on specific hardware and software versions, exact reproduction of the original experiments requires the corresponding **Novint Falcon**, CHAI3D v3.3.0, Windows environment, and experimental parameter configuration.
