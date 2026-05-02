# MKS Release System

This document defines the release model for MKS.

It exists to answer three questions clearly:
- what stage the language is currently in
- what kinds of releases exist
- what must be true before MKS can move to the next stability level

## Current stage

Current MKS stage:
- `Experimental language / unstable runtime line`

Why:
- core execution is usable
- some semantic contracts are now explicit
- major language areas are still unresolved or only partially frozen
- package/native extension direction is not stable yet

MKS is therefore:
- usable for experiments, internal tools, and language iteration
- not yet a stable language platform
- not yet a stable package/plugin target

## Release layers

MKS has three separate release surfaces:

### 1. Repository release

This answers:
- is this snapshot buildable
- do tests pass
- is it suitable for users to try

### 2. Language release

This answers:
- which language semantics are considered stable
- what user code is safe to rely on across versions

### 3. Runtime/ABI release

This answers:
- whether native extensions, embedding, or plugins can rely on a stable C/runtime contract

These three layers must not be confused.

A repo snapshot may be usable before the language is stable.
A stable language does not automatically imply a stable native ABI.

## Release kinds

### `dev`

Purpose:
- active branch work
- local builds
- no compatibility promise

Allowed:
- parser changes
- runtime changes
- semantic changes
- test reshaping
- internal refactors

Expectation:
- may break existing MKS code at any time

Suggested version form:
- `0.x-dev`

### `snapshot`

Purpose:
- public testing build from a known commit

Allowed:
- semantic changes are still allowed
- behavior may still change between snapshots

Expectation:
- builds
- test suite passes
- known unstable areas are documented

Suggested version form:
- `0.x-snapshot.N`

### `preview`

Purpose:
- freeze a narrower language subset for early adopters

Expectation:
- explicitly documented stable subset
- no intentional breaking changes inside that subset without a version bump and release note
- unresolved areas remain marked as non-stable

Suggested version form:
- `0.x-preview.N`

### `stable`

Purpose:
- public release line with compatibility expectations

Expectation:
- stable language contract for documented features
- release notes for changes
- compatibility policy is enforced

Suggested version form:
- `1.y.z`

## Language stages

### Stage 0: Experimental

Definition:
- syntax and runtime are usable
- semantics are still being discovered and frozen

Allowed:
- breaking language changes
- changing builtin APIs
- changing module/package behavior

Required signals:
- README and docs clearly say experimental

Current MKS status:
- `Yes`

### Stage 1: Structured experimental

Definition:
- major areas start to get written contracts
- unresolved areas are explicitly labeled
- changes are reviewed against docs, not only against code

Required:
- architecture contract
- semantics contract
- GC invariants
- test policy
- release model

Current MKS status:
- `Yes, partially achieved`

Reason:
- the contract layer now exists
- but too many language areas are still unstable to call the language preview-ready

### Stage 2: Language preview

Definition:
- a documented subset of the language is declared stable enough for early real projects

Before MKS may enter preview, all of these must be true:
- copy/reference model is frozen
- user-facing `null` story is frozen
- module import contract is frozen
- `watch` contract is either frozen or explicitly removed from the preview subset
- `extend` contract is frozen
- package import behavior is frozen for the preview subset
- release notes distinguish stable vs non-stable features

Current MKS status:
- `No`

### Stage 3: Language stable

Definition:
- documented language features keep compatibility across stable releases

Before MKS may enter stable, all of these must be true:
- preview subset has stayed stable across multiple preview releases
- remaining core semantic gaps are closed
- stable error/documentation expectations exist
- packaging story is good enough for real users
- breaking changes have an explicit policy

Current MKS status:
- `No`

### Stage 4: Stable runtime/ABI

Definition:
- native plugins/embedding can target a stable runtime surface

Before MKS may enter this stage, all of these must be true:
- plugin/native extension direction is chosen
- runtime ownership/lifetime rules are frozen for the public ABI
- ABI versioning exists
- native module test coverage exists

Current MKS status:
- `No`

## What is already stable enough to rely on today

This is not a full stable-language promise.
It is a practical list of features that are currently the least disputed:

- basic arithmetic and control flow
- function calls
- namespace-style imports
- top-level executable statements skipped during import
- current `Read` / `ReadLine` / `ReadWord` split
- current pointer baseline
- current RuntimeValue assignment model

These are still subject to change until preview/stable, but they are more frozen than the rest.

## What is not stable enough to advertise as stable

- full user-facing `null` surface
- full `watch` feature model
- full `extend` conflict/module model
- package management workflow
- plugin/native ABI
- long-term object model
- module cache lifetime as public contract

## Versioning policy

Until stable:
- use major version `0`
- minor version may contain breaking changes
- patch version is only for behavior/doc/test fixes within the current intended direction

Recommended meaning:
- `0.x.0` = new unstable line / new semantic wave
- `0.x.y` = fixes and small compatible updates inside that unstable line

After stable:
- `1.y.z`
- breaking changes require a major version bump

## Release gates

### Gate for `snapshot`

Must have:
- build succeeds
- `ctest --test-dir build --output-on-failure` passes
- `./tests.sh` passes
- known unstable areas remain documented

### Gate for `preview`

Must have:
- snapshot gate satisfied
- preview-stable subset explicitly listed
- docs do not overstate unresolved semantics
- targeted tests exist for every feature newly marked stable

### Gate for `stable`

Must have:
- preview gate satisfied across more than one release cycle
- compatibility rules documented
- release notes discipline exists
- remaining non-stable areas are either stabilized or excluded from the stable claim

## Release note rules

Every public release after `dev` should say:
- current stage
- what is newly frozen
- what is still non-stable
- whether any intentional break happened

Required wording discipline:
- do not say "stable" for a feature unless it is in the release's declared stable subset
- do not say "language stable" while major semantic areas remain unresolved

## Practical conclusion for MKS right now

The correct public wording today is:
- `experimental`
- `unstable language line`
- `snapshot/testing builds are acceptable`

The incorrect wording today is:
- `stable language`
- `stable package ecosystem`
- `stable plugin ABI`
