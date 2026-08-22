# DOA Xtreme 3 Venus (PCSH00250) — cheats

Found with Thor's memory search, driven entirely from the MCP server. The
method is written out below because it re-derives in about a minute, and that
matters: **a guest address is not guaranteed to survive a reboot of the title.**
Treat the address here as this-session-verified rather than eternal, and if it
does not take, redo the two-step search — it is quick.

## Zack Dollars (money)

```
0x816598B4   uint32   your money
```

Verified: poking it changed the casino readout from `99430` to `1234567`, and
the display followed the poke rather than the other way round.

There were twelve addresses holding `99430`. A bisect — reset all twelve, perturb
half, look at the readout, keep the half that moved — took it 12 → 6 → 3 → 1 in
three steps. The other eleven are copies or unrelated coincidences and are not
worth poking; writing to all of them is how you corrupt a save.

The game caps the displayed value at `/1500000`, so keep under that.

## How to re-derive it

Get to the casino, where the money is on screen, then:

```
mem_search  value=<the number you can see>  width=4
mem_poke    address=<candidate>  value=1234567  width=4
```

`99430` gave twelve candidates on a fresh save. Any distinctive number does; a
value like `1` or `100` will not, because half the heap holds those.

If more than a handful survive, bisect rather than poking them individually —
reset them all to the known value, perturb half, and see whether the number on
screen changes. That halves the set per screenshot.

## Getting into the game at all

The intro cannot be advanced with `adb shell input` — see the input note in
CLAUDE.md. Use the MCP `press` tool, which writes below SDL:

```
boot_title  title_id=PCSH00250  wait_for=Archive.psarc
press       button=circle          # past the autosave notice
press       button=circle          # through the intro, several times
press       button=circle          # confirm "Go to the Casino"
```

## Not done yet

Owner Level and EXP are both on screen and would search the same way. Level 1 is
a poor search value on its own, so the approach there is to earn EXP and narrow
on `greater` — the relative compares need no value at all.
