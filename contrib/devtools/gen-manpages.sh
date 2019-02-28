#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

pasteld=${pasteld:-$SRCDIR/pasteld}
PASTELCLI=${PASTELCLI:-$SRCDIR/pastel-cli}
PASTELTX=${PASTELTX:-$SRCDIR/pastel-tx}

[ ! -x $pasteld ] && echo "$pasteld not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
ZECVERSTR=$($PASTELCLI --version | head -n1 | awk '{ print $NF }')
ZECVER=$(echo $ZECVERSTR | awk -F- '{ OFS="-"; NF--; print $0; }')
ZECCOMMIT=$(echo $ZECVERSTR | awk -F- '{ print $NF }')

# Create a footer file with copyright content.
# This gets autodetected fine for pasteld if --version-string is not set,
# but has different outcomes for pastel-cli.
echo "[COPYRIGHT]" > footer.h2m
$pasteld --version | sed -n '1!p' >> footer.h2m

for cmd in $pasteld $PASTELCLI $PASTELTX; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=$ZECVER --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-$ZECCOMMIT//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
