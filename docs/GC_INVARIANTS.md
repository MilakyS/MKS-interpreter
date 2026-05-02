# MKS GC Invariants

This document defines the invariants that changes must preserve around GC-managed runtime data.

## Core rule

If a runtime object is GC-managed, every place that creates, marks, names, roots, traverses, or frees that object must stay consistent.

## Object-header rule

- GC-managed objects carry GC metadata in their object header.
- Code must not zero or overwrite a GC-managed object in a way that destroys header fields after allocation.
- Any post-allocation initialization must preserve the header layout.

## Reachability rule

A live object must be reachable through at least one of:
- an active GC root
- a reachable environment
- another marked GC-managed object
- a currently protected temporary runtime value

## Rooting rule

- Temporary runtime values that can outlive a local expression step must be rooted before operations that may trigger collection.
- Native helper boundaries must not assume a temporary is safe merely because it is on the C stack.

## Environment rule

- Environments are part of the reachability graph.
- If a runtime object stores an environment pointer, the GC traversal logic must keep that environment reachable.
- If a pointer-like runtime object references data owned by an environment, the reachability path must include that environment.

## New runtime type rule

When adding a new GC-managed runtime type, review all GC switch sites that classify or traverse object kinds, including:
- allocation type naming
- mark traversal
- root-needs checks where applicable
- free/destructor traversal

No new GC-managed type is complete until all of those paths are accounted for.

## Foreign-pointer rule

- Raw pointers stored inside runtime objects require an explicit owner.
- If the GC does not own the pointee, documentation or code must identify who invalidates it and when.
- Pointer-like objects must not silently outlive the storage they reference.

## Mutation rule

- Writes that reshape arrays, objects, environments, or pointer-target containers must preserve all reachability paths needed by the written values.
- If a write can replace one live object with another, the new object must remain reachable after the write completes.

## Design in progress

The repository does not yet expose a fully documented public embedding/plugin ABI.
Until that exists, treat all GC object-layout assumptions as internal contracts,
not stable external ABI.
