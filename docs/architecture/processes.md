# Process Management Overview

## List of Features

- [System structure](#system-structure)
- [Process subsystem scope](#process-subsystem-scope)
- [Ownership boundaries](#ownership-boundaries)
- [Core execution flows](#core-execution-flows)
- [Process model](#process-model)
- [Process control block](#process-control-block)
- [PID allocation](#pid-allocation)
- [Process states](#process-states)
- [Process lifecycle](#process-lifecycle)
- [Scheduler design](#scheduler-design)
- [Run queues and scheduling policy](#run-queues-and-scheduling-policy)
- [Priority management](#priority-management)
- [Yield and dispatch points](#yield-and-dispatch-points)
- [Context switching](#context-switching)
- [Trap frame and saved register state](#trap-frame-and-saved-register-state)
- [Kernel stacks](#kernel-stacks)
- [User process entry state](#user-process-entry-state)
- [Process address-space ownership](#process-address-space-ownership)
- [Page table lifetime](#page-table-lifetime)
- [Initial process bootstrap](#initial-process-bootstrap)
- [Process creation](#process-creation)
- [Fork semantics](#fork-semantics)
- [Exec state replacement](#exec-state-replacement)
- [Argument vector handoff](#argument-vector-handoff)
- [Exit semantics](#exit-semantics)
- [Wait and child reaping](#wait-and-child-reaping)
- [Parent-child relationships](#parent-child-relationships)
- [Orphan handling](#orphan-handling)
- [Process groups](#process-groups)
- [Per-process resource ownership](#per-process-resource-ownership)
- [File descriptor table ownership](#file-descriptor-table-ownership)
- [Current working directory ownership](#current-working-directory-ownership)
- [Blocking and wakeup paths](#blocking-and-wakeup-paths)
- [Sleep state](#sleep-state)
- [Signal state](#signal-state)
- [Signal delivery checkpoints](#signal-delivery-checkpoints)
- [Signal-driven process state changes](#signal-driven-process-state-changes)
- [System call integration](#system-call-integration)
- [Subsystem integration boundaries](#subsystem-integration-boundaries)
- [Error handling and cleanup policy](#error-handling-and-cleanup-policy)
- [Design tradeoffs and limits](#design-tradeoffs-and-limits)
- [Testing and debugging hooks](#testing-and-debugging-hooks)

## System Structure

# Detailed Architecture and Decisions

## Process Subsystem Scope

## Ownership Boundaries

## Core Execution Flows

### Boot to Init

### Runnable Process to CPU

### User Trap to Kernel Return

### Blocking to Wakeup

### Exit to Reap

## Process Model

## Process Control Block

### Identity Fields

### Scheduling Fields

### Execution Context Fields

### Address-Space Fields

### Resource Ownership Fields

### Signal Fields

### Relationship Fields

## PID Allocation

## Process States

## Process Lifecycle

### Allocation

### Initialization

### Runnable Entry

### Running State

### Blocked State

### Stopped State

### Zombie State

### Reaping

## Scheduler Design

## Run Queues and Scheduling Policy

## Priority Management

## Yield and Dispatch Points

## Context Switching

### Kernel Context Switch

### User Entry

### Kernel Return Path

## Trap Frame and Saved Register State

## Kernel Stacks

## User Process Entry State

## Process Address-Space Ownership

### User Page Table Reference

### Kernel Mapping Assumptions

### Heap and Stack Ownership

## Page Table Lifetime

### Creation

### Copy

### Replacement

### Destruction

## Initial Process Bootstrap

## Process Creation

### Kernel-Created Processes

### User-Created Processes

## Fork Semantics

## Exec State Replacement

### Preserved State

### Replaced State

### Failure State

## Argument Vector Handoff

## Exit Semantics

### Exit Status

### Resource Release

### Wakeup of Waiters

## Wait and Child Reaping

## Parent-Child Relationships

## Orphan Handling

## Process Groups

### Group Creation

### Group Membership

### Group-Directed Signals

## Per-Process Resource Ownership

## File Descriptor Table Ownership

### Descriptor Allocation

### Descriptor Inheritance

### Descriptor Cleanup

## Current Working Directory Ownership

## Blocking and Wakeup Paths

### Blocking Reasons

### Wakeup Reasons

### Scheduler Handoff

## Sleep State

## Signal State

### Handler Table

### Signal Mask

### Pending Signals

### Default Dispositions

## Signal Delivery Checkpoints

## Signal-Driven Process State Changes

## System Call Integration

### Process Lifecycle Syscalls

### Scheduling Syscalls

### Signal Syscalls

### Resource Ownership Syscalls

## Subsystem Integration Boundaries

### Memory Management Boundary

### Filesystem Boundary

### Trap Boundary

### Scheduler Boundary

### Signal Boundary

### Userspace Boundary

## Error Handling and Cleanup Policy

## Design Tradeoffs and Limits

## Testing and Debugging Hooks
