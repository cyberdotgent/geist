#!/bin/bash
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
#
# Fold a set of freshly built .deb files into an APT repository and sign it.
#
#   apt-repo.sh <repository-root> <incoming-dir> <component>
#
# `incoming` holds one directory per build, named debs-<codename>-<arch>, as
# the workflow's artifacts are downloaded. The repository accumulates: this
# adds to what is already there rather than rebuilding it from nothing, so a
# distribution whose build failed keeps the packages it had.
#
# Layout, with the codename as the suite and stable/unstable as components
# beside each other, so one line of sources.list picks a track:
#
#   pool/<component>/<codename>/<package>_<version>_<arch>.deb
#   dists/<codename>/<component>/binary-<arch>/Packages{,.gz}
#   dists/<codename>/{Release,Release.gpg,InRelease}
set -euo pipefail

ROOT=$(cd "$1" && pwd)
INCOMING=$(cd "$2" && pwd)
COMPONENT="$3"

ORIGIN="Geist"
LABEL="Geist"
DESCRIPTION="IBM BookManager BOO tools"
# How many builds of one package to keep on unstable. Unstable versions carry
# a UTC timestamp, so they sort lexically in build order and the newest are
# the tail. Stable is never pruned: a release stays downloadable.
KEEP_UNSTABLE=5

case "$COMPONENT" in
  stable|unstable) ;;
  *) echo "apt-repo.sh: component must be stable or unstable, not '$COMPONENT'" >&2; exit 1 ;;
esac

if [ -z "${APT_GPG_KEY_ID:-}" ]; then
  echo "apt-repo.sh: APT_GPG_KEY_ID is not set" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Take delivery
# ---------------------------------------------------------------------------

shopt -s nullglob
added=0
for dir in "$INCOMING"/debs-*; do
  [ -d "$dir" ] || continue
  name=${dir##*/debs-}
  codename=${name%-*}
  arch=${name##*-}
  debs=("$dir"/*.deb)
  if [ ${#debs[@]} -eq 0 ]; then
    echo "  $codename/$arch: nothing built, keeping what is already published"
    continue
  fi
  pool="$ROOT/pool/$COMPONENT/$codename"
  mkdir -p "$pool"
  for deb in "${debs[@]}"; do
    cp -f "$deb" "$pool/"
    added=$((added + 1))
  done
  echo "  $codename/$arch: ${#debs[@]} package(s)"
done

if [ "$added" -eq 0 ] && [ ! -d "$ROOT/pool" ]; then
  echo "apt-repo.sh: nothing to publish and no existing repository" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Prune superseded unstable builds
# ---------------------------------------------------------------------------

if [ "$COMPONENT" = unstable ]; then
  for pool in "$ROOT"/pool/unstable/*; do
    [ -d "$pool" ] || continue
    # Group by package name and architecture, which is everything in the file
    # name either side of the version.
    for key in $(ls "$pool" | sed -n 's/^\(.*\)_[^_]*_\([^_]*\)\.deb$/\1 \2/p' |
                 sort -u | tr ' ' '@'); do
      pkg=${key%@*}
      arch=${key#*@}
      mapfile -t versions < <(ls "$pool" | grep -E "^${pkg}_.*_${arch}\.deb$" | sort)
      excess=$(( ${#versions[@]} - KEEP_UNSTABLE ))
      if [ "$excess" -gt 0 ]; then
        for old in "${versions[@]:0:$excess}"; do
          echo "  pruning $(basename "$pool")/$old"
          rm -f "$pool/$old"
        done
      fi
    done
  done
fi

# ---------------------------------------------------------------------------
# Indices
# ---------------------------------------------------------------------------

cd "$ROOT"

# Every codename that has a pool under any component, so regenerating one
# component never drops the other's indices.
codenames=$(for c in pool/*/*/; do basename "$c"; done | sort -u)

for codename in $codenames; do
  components=""
  architectures=""
  for comp in stable unstable; do
    pool="pool/$comp/$codename"
    [ -d "$pool" ] || continue
    # An array, not `ls ... || continue`: nullglob is set, so a pattern that
    # matches nothing leaves `ls` with no arguments at all, whereupon it
    # lists the working directory and succeeds. That silently published an
    # architecture the pool had no packages for.
    all=("$pool"/*.deb)
    [ ${#all[@]} -gt 0 ] || continue
    components="$components $comp"

    for arch in amd64 arm64; do
      debs=("$pool"/*_"$arch".deb)
      [ ${#debs[@]} -gt 0 ] || continue
      case " $architectures " in *" $arch "*) ;; *) architectures="$architectures $arch";; esac

      out="dists/$codename/$comp/binary-$arch"
      mkdir -p "$out"
      # Paths in Packages are relative to the repository root, which is why
      # this runs from there.
      apt-ftparchive --arch "$arch" packages "$pool" > "$out/Packages"
      gzip -9fkn "$out/Packages"
      printf 'Archive: %s\nComponent: %s\nOrigin: %s\nLabel: %s\nArchitecture: %s\n' \
        "$codename" "$comp" "$ORIGIN" "$LABEL" "$arch" > "$out/Release"
    done
  done

  [ -n "$components" ] || continue

  # shellcheck disable=SC2086
  apt-ftparchive \
    -o APT::FTPArchive::Release::Origin="$ORIGIN" \
    -o APT::FTPArchive::Release::Label="$LABEL" \
    -o APT::FTPArchive::Release::Suite="$codename" \
    -o APT::FTPArchive::Release::Codename="$codename" \
    -o APT::FTPArchive::Release::Architectures="$(echo $architectures)" \
    -o APT::FTPArchive::Release::Components="$(echo $components)" \
    -o APT::FTPArchive::Release::Description="$DESCRIPTION" \
    release "dists/$codename" > "dists/$codename/Release.tmp"
  mv "dists/$codename/Release.tmp" "dists/$codename/Release"

  # Both signatures: InRelease is what modern apt fetches, Release.gpg is
  # what an older one falls back to.
  rm -f "dists/$codename/InRelease" "dists/$codename/Release.gpg"
  gpg --batch --yes --default-key "$APT_GPG_KEY_ID" \
      --clearsign -o "dists/$codename/InRelease" "dists/$codename/Release"
  gpg --batch --yes --default-key "$APT_GPG_KEY_ID" \
      -abs -o "dists/$codename/Release.gpg" "dists/$codename/Release"

  echo "  dists/$codename:$components ($(echo $architectures))"
done
