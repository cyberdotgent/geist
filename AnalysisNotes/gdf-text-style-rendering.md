# GDF Text Style Rendering Choices

Date: 2026-08-30

This note records a *rendering* decision, not a BOO container fact. It was moved
here out of `Format/GDF.md` so that the format note stays a specification of the
file format. See `Format/GDF.md`, "Character Set Selection", for the container
side.

## Problem

A GDF character order (`0xc3`, `0x83`) carries EBCDIC text bytes but no font.
The typeface comes from the character-set state established by the `0x38`
Character Set order, whose operand is a local character-set id (`LCID`). IBM
`QPRG1GDR` topic `B.11.3` documents the `LCID` ranges: `0x00` is the default
set and `0x41..0xdf` are user-defined sets. A GDF stream may carry a font list
binding each `LCID` to an eight-byte GDDM character-set name.

No `.BOO` fixture in this repository carries such a font-list prolog, so there
is no fixture evidence for the binding, and no hosted rendering isolates a
single character set well enough to infer one.

## What `libgeist` does

`libgeist` renders text with its own bitmap glyphs, so it needs only a coarse
style trait -- serif/sans/monospaced, plus bold and italic -- rather than a real
typeface. It maps the GDDM character-set names below onto those traits.

| GDDM character-set name | Style trait used |
| --- | --- |
| `ADMDVECP` | monospaced/modern |
| `ADMUUARP` | serif |
| `ADMUUCIP` | serif italic |
| `ADMUUCRP` | serif |
| `ADMUUCSP` | script |
| `ADMUUDRP` | sans |
| `ADMUUFSS` | sans |
| `ADMUUGEP` | serif |
| `ADMUUGGP` | serif |
| `ADMUUGIP` | serif |
| `ADMUUKRF` | sans bold |
| `ADMUUKRO` | sans bold |
| `ADMUUKSF` | sans bold |
| `ADMUUKSO` | sans bold |
| `ADMUUMOD` | monospaced/modern |
| `ADMUUNSF` | sans narrow |
| `ADMUUNSO` | sans narrow |
| `ADMUUORP` | serif |
| `ADMUUSHD` | sans |
| `ADMUUSRP` | monospaced/modern |
| `ADMUUTIP` | serif bold italic |
| `ADMUUTRP` | serif bold |
| `ADMUUTSS` | sans bold |

The names themselves are IBM GDDM symbol-set names. The style traits are a
`libgeist` choice, chosen so that a rendered figure reads correctly at a glance;
they are not a claim about the historical typefaces.

## Status

Unverified against fixtures. Whenever a kind `G` fixture with a font-list
prolog is added to `BOO/`, this mapping should be checked against a hosted
BookServer GIF for the same resource, and any binding that the fixture actually
establishes should move into `Format/GDF.md` with that evidence.
