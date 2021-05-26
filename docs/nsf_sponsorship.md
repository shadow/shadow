# National Science Foundation Sponsorship

**Project Title:** Expanding Research Frontiers with a Next-Generation Anonymous Communication Experimentation (ACE) Framework

**Project Period:** October 1, 2019 - September 30, 2022

**Abstract:** [NSF Award Abstract #1925497](https://www.nsf.gov/awardsearch/showAward?AWD_ID=1925497)

The goal of this project is to develop a scalable and mature deterministic network simulator, capable of quickly and accurately simulating large networks such as [Tor](https://www.torproject.org). This project builds on [the Shadow Simulator](https://shadow.github.io/).

## NSF Project Overview

ACE will be developed with the following features:

 * **Application Emulation.** Learning from the community’s experience, ACE will directly execute software and run applications as normal operating system processes. By supporting the general execution of applications (i.e., anything that can be executed as a process: network servers, web browsers, scripts, etc.), ACE will support software independent of the programming language chosen by developers, and ACE will maximize its applicability to a large range of evaluation approaches that CISE researchers choose to utilize. As a result, ACE will be well-suited to website fingerprinting and censorship circumvention research focus areas, which typically require running a variety of tools written in a variety of languages.
 * **Network Simulation.** ACE will feature a light-weight network simulation component that will allow applications to communicate with each other through the ACE framework rather than over the Internet. ACE will simulate common transport protocols, such as TCP and UDP. ACE will also simulate virtual network routers and other network path components between end-hosts, and support evaluation under dynamic changes to timing, congestion, latency, bandwidth, network location, and network path elements. Therefore, ACE will support both network-aware and location-aware anonymous communication research and allow researchers to continue to advance this research agenda in current and future Internet architectures.
 * **Function Interposition.** ACE will utilize function interposition in order to connect the processes being run by the operating system to the core network simulation component. ACE will support an API of common system calls that are used to, e.g., send and receive data to and from the network. Therefore, all processes executed in ACE will be isolated from the Internet and connected through ACE’s simulated network, and the simulation component will drive process execution.
 * **Controlled, Deterministic Execution.** ACE features a deterministic discrete-event engine, and will therefore control time and operate in simulated timescales. As a result, ACE will be disconnected from the computational abilities of the host machine: ACE will run as-fast-as-possible, which could be faster or slower than real time depending on experiment load. ACE is deterministic so that research results can be independently and identically reproduced and verified across research labs.
 * **Parallel and Distributed Execution.** ACE will rely on the operating system kernel to run and manage processes. Operating system kernels have been optimized for this task, and ACE will benefit in terms of better performance and a smaller code base. Moreover, ACE will be embarrassingly parallel: the Linux kernel generally scales to millions of processes that can be run in parallel, and we will design ACE such that any number of processes can be executed across multiple distinct machines. Therefore, ACE will scale to realistically-sized anonymous communication networks containing millions of nodes, and can be deployed on whatever existing infrastructure is available at community members' institutions.

As part of the ACE framework, we will also develop a **user interface** to control and monitor the experimental process, a **toolkit** to help users set up and configure experiments (including network, mobility, and traffic characteristics and models) and to visualize results, and a **data repository** where researchers can share and archive experimental results.

## Project Goals/Activities

Here we outline some high level tasks that we are completing or plan to complete under this project. We are using Github for project development, including for tracking progress on major milestones and development tasks. We provide an outline of our agenda here, and link to the appropriate Github page where appropriate. Tasks without corresponding Github links means we don't yet have progress to share at this time.

 * **Task 0: Investigate Architectural Improvements**
   * Build prototype of a process-based simulation architecture - [milestone](https://github.com/shadow/shadow/milestone/16)
   * Evaluate and compare against a plugin-based simulation architecture
   * Decide which architecture is right for ACE

 * **Task 1: Develop Core ACE System**
   * Improve test coverage and infrastructure - [shadow milestone](https://github.com/shadow/shadow/milestone/15), [shadow-plugin-tor milestone](https://github.com/shadow/shadow-plugin-tor/milestone/1)
   * Enable new code to be written in Rust - [milestone](https://github.com/shadow/shadow/milestone/17)
   * Improve consistency of simulation options and configuration
   * Improve maintainability and accuracy of TCP implementation - [milestone](https://github.com/shadow/shadow/milestone/18)
   * Simplify event scheduler, implement continuous event execution model
   * Build a distributed core simulation engine
   * Develop CPU usage model to ensure plugin CPU utilization consumes simulation time

 * **Task 2: Develop User Interface and Visualizations**
   * Design control protocol and API for interacting with Shadow
   * Specify/document protocol
   * Develop user interface that uses the control API
   * Improve tools for analyzing and understanding simulation results

 * **Task 3: Develop Simulation Models for ACE**
   * Improve tools for generating and configuring private Tor networks
   * Improve tools for generating and configuring background traffic models
   * Improve tools for modeling Internet paths and latency
   * Develop support for mobile hosts
   * Create realistic host mobility models

 * **Task 4: Engage Community**
   * Create data repository where users can share configs and results
   * Create user outreach material and surveys to collect feedback
   * Improve user documentation and usage instructions

Over all tasks, we plan to significantly improve documentation, test coverage, and code maintainability.

## People
 * [Rob Jansen](https://www.robgjansen.com) - Project Leader, Principal Investigator, U.S. Naval Research Laboratory
 * Roger Dingledine - Principal Investigator, The Tor Project
 * Micah Sherr - Principal Investigator, Georgetown University
 * Jim Newsome - Developer, The Tor Project
 * Steven Engler - Developer, Georgetown University / The Tor Project
