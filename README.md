# AutoBike Trajectory MPC

## Overview

C implementation of the 2022 AutoBike MPC group's matlab code using qpSWIFT.
Will be added to the AutoBike fork once completed.

## Installation

Make sure you use `git clone --recursive` to get the qpSWIFT submodule, then follow the instructions in the qpSWIFT README to build the library.

## Usage

Configure `mpc_params` and `x0` in `mpc_trajectory:main.c`, then run `make` to rebuild and run.
