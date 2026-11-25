# Beat sync

Study and design for a beat sync of our own, 2026-11-25, for the user to discuss. Nothing is built yet.

The beat sync lets CoolSynth, CoolSynth 303 and CoolSynth 808 keep one tempo, one beat grid and, if wanted, one
play/stop state with each other and with any other program on the local network that speaks the same protocol, DAWs
among them.
- It is our own code, MIT licensed, written from this description.
- It speaks an existing network protocol byte for byte, so other programs that speak it hear us as one of their own.
- It will live in the engine repository (coolsynth-engine), so the three apps share one implementation.

This document holds everything an implementer needs: the bytes on the wire, the rules every peer follows, the maths
every peer must agree on, the edge cases, our design for the engine, and a test plan.

## Words used here

A peer is one running program taking part, here one app with the beat sync switched on.

A peer id is 8 random bytes a peer draws when it starts a new session of its own.

A session is the group of peers that play together.
- Its session id is the peer id of the peer that founded it.
- Peers in different sessions on the same network notice each other, and one side moves over (see "Choosing and
  joining a session").

The host clock is a peer's own steady clock in microseconds: QueryPerformanceCounter on Windows, mach_absolute_time
on macOS.

The session clock is a clock in microseconds that all peers of a session share.
- It is zero at the moment the session was founded, on the founder's host clock.
- Each peer keeps a clock offset: session time = host time + offset.
- The founder's offset is minus its host time at founding; every other peer measures its offset over the network.

The session timeline is the mapping between session time and beats, given by three numbers: a tempo, an anchor beat
and the anchor's session time.

A micro-beat is a millionth of a beat; beats travel and are computed as whole micro-beats.

The quantum is a number of beats (4 for one bar of 4/4) that an app chooses; peers with the same quantum agree on
where each quantum starts.
- The phase of a beat is its position inside the quantum, from 0 up to the quantum.
- Phase is counted from beat 0 of the session timeline, so beat 0, beat 4, beat 8 and so on start a 4-beat quantum.

The play state is whether the session plays, with the beat at which that changed and when.

A gateway is the set of sockets a peer opens on one local network address; a computer with Wi-Fi, Ethernet and the
loopback interface has three.

## The wire

### Basic rules

Everything multi-byte is big-endian (network byte order).

Integers are two's complement; a time or a beat count is a signed 64-bit integer.

Every datagram is at most 511 bytes; a sender never builds a larger one, a receiver reads into a 512-byte buffer and
drops anything that does not fit.

There are two kinds of datagram, told apart by their first eight bytes:
- Discovery: `5F 61 73 64 70 5F 76 01`, the ASCII of `_asdp_v` followed by the byte 1.
- Measurement: `5F 6C 69 6E 6B 5F 76 01`.

### Discovery datagrams

A discovery datagram is a 20-byte header followed by a payload.

The header:
- Bytes 0 to 7: the discovery magic above.
- Byte 8: the message type.
  - 1, announce: "here I am and this is my state". Sent to the multicast group.
  - 2, reply: the answer to an announce, with the same payload. Sent by unicast to the announcer.
  - 3, leaving: "I am going". Sent to the multicast group, with no payload.
  - Any other value is ignored.
- Byte 9: the time to live, in seconds. 5 in announces and replies, 0 in leaving messages.
- Bytes 10 and 11: a group number, always 0. A datagram with any other value is ignored.
- Bytes 12 to 19: the sender's peer id.

A receiver ignores a datagram with the wrong magic, one shorter than 20 bytes, one whose group is not 0, and one
carrying its own peer id (a peer hears its own multicasts on the loopback interface).

### The payload

A payload is a run of entries, each:
- a 4-byte key, the ASCII of four characters read as a big-endian unsigned integer;
- a 4-byte size, the number of value bytes that follow;
- the value.

Rules for reading a payload:
- Entries can come in any order; senders use the order listed below.
- An entry whose key a peer does not know is skipped by its size.
- An entry whose size runs past the end of the datagram makes the whole datagram invalid: drop it.
- A known entry whose value does not parse to exactly its size makes the whole datagram invalid: drop it.
- An entry with a size of zero is never sent; a sender leaves out a value that has nothing to say.

The entries of an announce or reply, in the order a peer sends them:

`tmln`, key `74 6D 6C 6E`, 24 bytes: the session timeline as the sender sees it.
- Bytes 0 to 7: the tempo as whole microseconds per beat, signed 64-bit. 120 BPM is 500000.
- Bytes 8 to 15: the anchor beat in micro-beats, signed 64-bit.
- Bytes 16 to 23: the anchor's session time in microseconds, signed 64-bit.

`sess`, key `73 65 73 73`, 8 bytes: the session id, the founder's peer id, verbatim.

`stst`, key `73 74 73 74`, 17 bytes: the session's play state.
- Byte 0: 1 if playing, 0 if stopped. A receiver reads any non-zero byte as playing.
- Bytes 1 to 8: the session beat, in micro-beats, at which it starts or stops playing.
- Bytes 9 to 16: the session time, in microseconds, at which the change was made; the newest change wins.
- A peer that has seen no change since it founded or joined its session sends playing 0, beat 0, time 0.

`mep4`, key `6D 65 70 34`, 6 bytes: where to send clock measurement pings to this peer over IPv4.
- Bytes 0 to 3: the IPv4 address, most significant byte first.
- Bytes 4 and 5: the UDP port.
- It is the address of the gateway the datagram leaves from and the port of that gateway's measurement socket.

Entries other peers may send, which a first version reads past:
- `mep6`, key `6D 65 70 36`, 18 bytes: the same for IPv6, 16 address bytes then a 2-byte port. Sent instead of
  `mep4` from an IPv6 gateway.
- `aep4`, key `61 65 70 34`, 6 bytes, and `aep6`, key `61 65 70 36`, 18 bytes: where the peer takes audio streams, for
  peers that share audio over the network.
- `artl`, key `61 72 74 6C`, 24 bytes: a shared song position, for peers that share one (three signed 64-bit values:
  a beat offset in micro-beats, the beat at which playing was launched in micro-beats, and a session time stamp in
  microseconds). Its rules are new and still changing at the other programs' side; we leave it out.

A full IPv4 announce with the four entries is 107 bytes: 20 of header, 32 for `tmln`, 16 for `sess`, 25 for `stst`
and 14 for `mep4`.

A leaving message is the 20-byte header alone, type 3, time to live 0.

### Measurement datagrams

A measurement datagram is the measurement magic, one byte of message type and a payload of entries as above.
- Type 1 is a ping, type 2 a pong; anything else is ignored.

Its entries:
- `__ht`, key `5F 5F 68 74`, 8 bytes: a host time of the pinging peer, in microseconds.
- `__gt`, key `5F 5F 67 74`, 8 bytes: a session time of the answering peer, in microseconds.
- `_pgt`, key `5F 70 67 74`, 8 bytes: the `__gt` of the previous pong, sent back in the next ping.
- `sess`, as above: the answering peer's session id.

A ping carries `__ht`, and from the second ping of a measurement on also `_pgt`: a payload of 16 or 32 bytes.

A pong carries `sess`, then `__gt`, then the ping's payload copied byte for byte.

### Byte examples

An announce from peer `ABCDEFGH` (bytes 41 to 48), founder of its own session, at 120 BPM, anchor beat 0 at session
time 0, stopped, measurement socket at 192.168.1.20 port 50123:

```
5F 61 73 64 70 5F 76 01 01 05 00 00 41 42 43 44 45 46 47 48     header: announce, ttl 5, group 0, id
74 6D 6C 6E 00 00 00 18 00 00 00 00 00 07 A1 20                 tmln, 24 bytes, 500000 us per beat
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00                 anchor beat 0, anchor time 0
73 65 73 73 00 00 00 08 41 42 43 44 45 46 47 48                 sess, 8 bytes, the session id
73 74 73 74 00 00 00 11 00 00 00 00 00 00 00 00 00              stst, 17 bytes, stopped, beat 0
00 00 00 00 00 00 00 00                                         time 0
6D 65 70 34 00 00 00 06 C0 A8 01 14 C3 CB                       mep4, 6 bytes, 192.168.1.20:50123
```

The same peer's leaving message:

```
5F 61 73 64 70 5F 76 01 03 00 00 00 41 42 43 44 45 46 47 48
```

A timeline at 128 BPM (468750 microseconds per beat), anchor beat 16.5 (16500000 micro-beats) at session time
90000000:

```
74 6D 6C 6E 00 00 00 18 00 00 00 00 00 07 27 0E 00 00 00 00 00 FB C5 20 00 00 00 00 05 5D 4A 80
```

A play state "playing from beat 16.5, changed at session time 90000123":

```
73 74 73 74 00 00 00 11 01 00 00 00 00 00 FB C5 20 00 00 00 00 05 5D 4A FB
```

A negative value, session time -7: `FF FF FF FF FF FF FF F9`.

A first ping at host time 1000000 (25 bytes):

```
5F 6C 69 6E 6B 5F 76 01 01 5F 5F 68 74 00 00 00 08 00 00 00 00 00 0F 42 40
```

Its pong from session `ABCDEFGH` at session time 5000250 (57 bytes):

```
5F 6C 69 6E 6B 5F 76 01 02 73 65 73 73 00 00 00 08 41 42 43 44 45 46 47 48
5F 5F 67 74 00 00 00 08 00 00 00 00 00 4C 4C 3A 5F 5F 68 74 00 00 00 08 00 00 00 00 00 0F 42 40
```

The next ping, sent at host time 1000500 (41 bytes):

```
5F 6C 69 6E 6B 5F 76 01 01 5F 5F 68 74 00 00 00 08 00 00 00 00 00 0F 44 34
5F 70 67 74 00 00 00 08 00 00 00 00 00 4C 4C 3A
```

## A peer's life

### Starting

When the beat sync is switched on, the peer founds a session of its own, even if it had one before.
- It draws a new random peer id; its session id is that id.
- Its clock offset makes session time zero now.
- Its session timeline takes the app's tempo and the app's current beat as the anchor beat at session time 0, so the
  app's beat neither jumps nor changes phase.
- Its play state is "nothing seen" (playing 0, beat 0, time 0).

Founding fresh on every start is what keeps a newcomer from taking over a running session: a new session is the
youngest one on the network, and the oldest session wins when two meet.

The peer id can be any 8 random bytes; we draw each byte from 33 to 126 so a log can print an id as text.

### Gateways and sockets

A peer opens one gateway per IPv4 address of the computer, the loopback address 127.0.0.1 included.
- Other programs on the same computer meet us through the loopback gateway, other computers through the others.
- The list of addresses is read again every 5 seconds; a new address gets a gateway, a vanished one loses its gateway
  (with a leaving message on it if that still goes out).
- A failed send on a gateway closes that gateway and reads the addresses again at once, which rebuilds it if its
  address is still there.

A gateway has three UDP sockets.

The group socket receives announces and leaving messages.
- It allows address reuse (SO_REUSEADDR; on macOS also SO_REUSEPORT), as every program on the computer binds the
  same port.
- It is bound to 0.0.0.0, port 20808.
- It joins the multicast group 224.76.78.75 on the gateway's address (IP_ADD_MEMBERSHIP with that interface).
- Multicast loopback (IP_MULTICAST_LOOP) is on for the loopback gateway and off for the others.
  - On Windows this option decides what the socket receives: with it off, the socket hears nothing sent from this
    computer, so local programs are heard only through the loopback gateway, once.
  - On macOS it decides what a socket sends; the same settings give the same result.
- On Linux, IP_MULTICAST_ALL is switched off so the socket hears only the group it joined on its own interface.

The discovery socket sends.
- It allows address reuse, is bound to the gateway's address with a port the system picks, and sends multicast out of
  that interface (IP_MULTICAST_IF); multicast loopback as for the group socket.
- It sends announces and leaving messages to 224.76.78.75 port 20808, and replies by unicast.
- It receives the replies others send to it.

The measurement socket pings and answers pings.
- Set up as the discovery socket, on its own system-picked port; that port goes into `mep4`.

The multicast hop limit stays at the system's default of 1, so announces stay on the local network segment.

On Windows, each socket gets the SIO_UDP_CONNRESET control set to off, and a receive that fails for any reason other
than the socket closing is followed by the next receive.
- Without both, a single "port unreachable" from a peer that has just quit can stop a socket from hearing anything
  again.

### Announcing

A gateway announces as soon as it opens, then every 250 milliseconds.

Whenever the peer's state changes (timeline, play state, session), every gateway announces at once, but never sooner
than 50 milliseconds after its previous announce or reply; a change inside that time goes out when the 50 ms are up.

Every announce received from another peer is answered with a reply: type 2, time to live 5, the same full payload as
an announce, sent from the discovery socket by unicast to the address and port the announce came from.

Replies are not answered.

### Hearing others

A peer keeps a table of the peers it hears, one row per peer id and gateway.
- A row holds the peer's session id, its timeline, its play state, its measurement address and an expiry time.
- An announce or reply sets the expiry to now plus the header's time to live (5 seconds); a row is removed one second
  after its expiry passes, so a silent peer goes after about 6 seconds.
- A leaving message removes that peer's row on that gateway at once.
- A gateway that closes removes all its rows.
- A peer seen through two gateways has two rows and counts once.
- An announce or reply without `tmln` or `sess` is ignored, as is one whose tempo is zero or negative.

The number of peers shown to the user is the number of different peer ids in our own session.

Each announce or reply is then handled in this order, because a timeline can make us change session before the play
state is looked at:
- If the sender is in our session, its timeline goes through the timeline rule, then its play state through the play
  state rule (both below).
- If the sender is in another session, that session's best timeline is kept (highest anchor beat), and if the session
  is new to us, a clock measurement against one of its peers starts.
- Then the peer count is taken again.

### Measuring a clock offset

To compare sessions, and to play in one, a peer needs the offset between its host clock and the session clock of the
session in question. It measures it with a run of pings to one peer of that session.

Which peer: the session's founder if it is in the table (its peer id equals the session id), otherwise the first
member in the table's order. The ping goes to the address in that peer's `mep4`, from the measurement socket of the
gateway the peer was seen on.

The exchange:
- We send a ping with `__ht` = our host time, and start a 50 ms timer.
- The answering peer replies at once with a pong: its `sess`, `__gt` = its session time now, then our ping's payload as
  it came.
  - It answers only type 1 datagrams whose payload is 32 bytes or less, and sends the pong from its measurement
    socket to the ping's source.
- When a pong comes back, we check its `sess`: if it is not the session we are measuring, the measurement fails.
- Otherwise we note our host time now (h2), send the next ping at once with `__ht` = h2 and `_pgt` = the pong's `__gt`,
  and restart the 50 ms timer.
- From each pong we take up to two estimates of the offset, using its `__gt` (gt), the `__ht` it echoes (ht, the host
  time that ping left) and the `_pgt` it echoes (pgt), all as doubles:
  - gt - (h2 + ht) / 2, when gt and ht are both non-zero: the answer's session time against the middle of our round
    trip.
  - (gt + pgt) / 2 - ht, when pgt is non-zero: our ping's send time against the middle of the two answers around it.
- When the timer runs out, we send a fresh ping with `__ht` only; when it runs out a sixth time in one measurement
  (five resends in all), the measurement fails.
- When we hold more than 100 estimates (about 51 round trips), the measurement is done: the offset is the median of the
  estimates, rounded to the nearest microsecond.
  - The median of an even count is the mean of the two middle values.

Session time is then host time plus that offset. The two clocks are taken to run at the same rate; drift is handled by
measuring again.

When measurements happen:
- When a session we did not know appears.
- 30 seconds after we joined another session, and every 30 seconds after that, against our current session, so the
  offset follows the drift between two computers' clocks. A founder that never joined anyone never re-measures: its
  clock defines the session's.
- A failed re-measurement of our own session is tried again 30 seconds later.
- A failed measurement of another session forgets that session and its rows; when it is heard again, it is measured
  again as new. Our design waits at least one second between attempts at the same session.

Pongs are matched to their measurement by the address they come from, so several measurements can run at once.

### Choosing and joining a session

When a measurement of another session succeeds, we compare the two session clocks at the same host time: the
difference is the other session's offset minus ours, in microseconds.
- More than 500000 (the other session is older by more than half a second): we join it.
- Between -500000 and 500000, exclusive: the session with the smaller session id wins, comparing the 8 bytes as
  unsigned numbers from the first byte on; we join it if its id is smaller than ours.
- Otherwise we stay.

Both sides make the same comparison from their own end, so they agree and one side moves.

Joining a session:
- Our session id becomes the other's; our peer id stays.
- Our clock offset becomes the measured one.
- Our session timeline becomes the best timeline we have seen for that session.
- The session's play state is taken over as the newest one we have seen among its peers, but it is not applied to the
  app: a peer that joins does not start or stop because the session already plays or not; it follows later changes
  only.
- The app's beat stays continuous and then moves to the session's phase, by at most half a quantum (see "The maths").
- We announce at once on every gateway, and re-measure in 30 seconds.
- The session we left stays in our list of known sessions, so it is not measured again while we know it.

The other members of a losing session do the same on their own, so a whole session moves over within a second or so.

### The session timeline and its changes

The timeline rule: a timeline heard for our own session replaces ours only if its anchor beat is larger than ours.
- An equal or smaller anchor beat is ignored.
- A replaced timeline is announced at once, so every peer passes a change on.
- Its tempo is clamped to 20 to 999 BPM first (see "The maths").

The anchor beat is the priority: a peer that changes the timeline always gives the new one a larger anchor beat than
the one it replaces, so the newest change wins everywhere.

A tempo change made in our app, at session time g:
- b = the session beat at g on the current timeline.
- The new timeline has the new tempo, anchor beat b* = the larger of b and the current anchor beat plus one beat
  (1000000 micro-beats), and anchor time g plus the time b* - b takes at the new tempo.
- So the beat grid is continuous at g, and the anchor beat grows.
- It is announced at once.

How a change spreads: our app changes the tempo in a block; within a few milliseconds the network thread makes the new
timeline and announces it; each peer that hears it adopts it, announces it in turn, and its app takes it at its next
audio block. On a local network the whole session follows within tens of milliseconds.

When two peers change the tempo at the same moment, the one whose anchor beat ends up larger wins everywhere; the
other app follows it at once.

### Play state

The play state rule: a play state heard for our own session replaces ours if its time is later than ours.
- A replaced play state is announced at once.
- Peers pass play states on even when their own app has start/stop sharing off.
- Our app follows a replaced play state only if start/stop sharing is on in our app, and only when it changes between
  playing and stopped.

A play or stop pressed in our app, at host time t:
- With start/stop sharing on, the network thread makes the play state: playing or not, beat = the session beat at t,
  time = the session time now (made later than the current play state's time if it is not).
- With start/stop sharing off, the app starts or stops on its own and nothing goes out.

The play state carries the moment the user pressed, not the moment the app really starts: each peer starts at its own
next quantum boundary after that beat, so peers with the same quantum start on the same downbeat.

### Leaving and being left

A peer that switches the beat sync off or quits sends a leaving message on each gateway.

A peer that vanishes without one is dropped after about 6 seconds.

When the last other peer of our session goes (by leaving, timing out, joining another session or a gateway closing),
we found a new session on our own, exactly as at the start: a new random peer id, a new session id, session time zero
now, the anchor beat = the session beat now on the old timeline, so the app's beat and phase carry on unchanged; the
play state becomes "nothing seen"; known sessions and rows are forgotten.
- This is why a lone peer always has a fresh session: it then loses against any session older than half a second.

When other peers remain, nothing changes: the session keeps its id and timeline even if its founder left.

## The maths every peer must agree on

Units:
- Time: signed 64-bit microseconds, of the host clock or the session clock.
- Beats: signed 64-bit micro-beats.
- Tempo: signed 64-bit whole microseconds per beat; BPM = 60000000 / microseconds per beat.

Every peer rounds the same way: llround (to nearest, halves away from zero) on a double, in this order.
- Beats in a duration d at tempo m: llround((double) d / (double) m * 1e6).
- Duration of b micro-beats at tempo m: llround((double) b / 1e6 * (double) m).
- A beat count from a beat value in beats: llround(beats * 1e6).

A timeline (m, B, T) maps:
- the beat at session time s: B + beats in (s - T) at m;
- the session time of beat b: T + duration of (b - B) at m.

Example, timeline (500000, 2000000, 1000000), 120 BPM with beat 2 at 1 second:
- At session time 1750000 the beat is 2000000 + llround(750000 / 500000 * 1e6) = 3500000, beat 3.5.
- Beat 5 (5000000) is at 1000000 + llround(3.0 * 500000) = 2500000.

Session and host time: session = host + offset, host = session - offset, both exact integer additions.

Tempo, both ways:
- From BPM: clamp the BPM to 20 to 999, then microseconds per beat = llround(60000000 / BPM).
- From the wire: BPM = 60000000 / microseconds per beat, clamped to 20 to 999, then microseconds per beat again from
  that BPM. A value of zero or below makes the datagram invalid.
- The beat maths always uses the whole microseconds per beat, never the BPM, so peers compute identical grids.
- 120 BPM is 500000, 128 is 468750, 140 is 428571 (140.00014 BPM), 20 is 3000000.
- 999 BPM is llround(60060.06) = 60060, which reads back as 999.000999 BPM, clamps to 999 and encodes as 60060 again.
- A clamp keeps the timeline's anchor beat and anchor time; only the tempo changes.

Phase of b in a quantum q (both in micro-beats): the remainder of b divided by q, taken in [0, q) also for negative b.
A quantum of zero gives a phase of zero.
- phase(9 beats, 4) = 1 beat; phase(-1 beat, 4) = 3 beats; phase(-4 beats, 4) = 0.

The phase that peers agree on is the phase of the session beat: quantum boundaries are the session beats that are
whole multiples of q, measured from session beat 0. Two peers with quantum 4 have their bar lines at the same moments;
a peer with quantum 8 has a bar line at every second one of them.

The app's own beat count is free in size but tied in phase:
- app beat at time t = session beat at t + the app offset, where the app offset is a whole multiple of the quantum.
- So the app's beat 3 can be the session's beat 11, but not its beat 12.
- When something moves the app offset off a multiple of q (joining a session, a new grid, a new quantum), it is
  snapped to the multiple of q nearest to it, ties going down: snap(o) = q * ceil((o - q / 2) / q), with q / 2 as
  llround(0.5 * q). So the app's beat moves by at most half a quantum, and only when its phase was off.
- Example: quantum 4, session beat 2.5, the app's free count says 13.5 (offset 11). snap(11) = 12, so the app's beat
  is 14.5 with phase 2.5; it moved by one beat.
- With a quantum of zero the offset is never snapped.

A quantized start: the app pressed play when the session beat was B; it starts at session beat S, the smallest whole
multiple of q at or after B, and its own beat 0 falls there (app offset = -S).
- Example: B = 13.7 beats, q = 4: S = 16. The app's beat is -2.3 at the press and counts in to 0.
- A start whose S is already past plays at once from the right beat.
- When no other peer is in the session, the app starts at once instead: the session grid is shifted by the smallest
  amount (at most half a quantum) that puts a quantum boundary at the press, as a timeline change that passes through
  that boundary at that moment.

The tempo-change rule as numbers: timeline (500000, 8000000, 10000000), change to 140 BPM (428571) at session time
12345678.
- b = 8000000 + llround(2345678 / 500000 * 1e6) = 12691356.
- b* = max(12691356, 8000000 + 1000000) = 12691356; the new timeline is (428571, 12691356, 12345678).
- Half a second later the old timeline says 13691356, the new one 13858024: the new grid runs faster from the change
  on.

The clock offset is the median of the estimates, rounded to the nearest microsecond. An example:
- A ping leaves at host time 1000000, its pong says session time 5000250 and arrives at host time 1000500: the
  estimate is 5000250 - 1000250 = 4000000.
- The next ping leaves at 1000500, its pong says 5000760 and arrives at 1001010: the estimates are
  5000760 - 1000755 = 4000005 and (5000760 + 5000250) / 2 - 1000500 = 4000005.

## Edge cases

Several network interfaces:
- One gateway each; a peer heard on two counts once and is measured through the first.
- Virtual adapters (VPN, Hyper-V, WSL) get gateways too; they cost a few datagrams and do no harm.
- A Wi-Fi network that drops and comes back makes its gateway vanish and return at the next 5-second scan; peers on
  the loopback gateway are not touched.
- A firewall that blocks unicast but lets multicast through gives peers we hear but cannot measure: we retry, slowly,
  and stay apart. Windows asks the first time an app opens its sockets; the app must be allowed on private networks to
  sync with other computers. The loopback interface is not filtered.

A sleeping computer:
- Every peer drops a sleeper after about 6 seconds; the sleeper, on waking, finds its timers long overdue.
- Windows' QueryPerformanceCounter counts through sleep; macOS's mach_absolute_time stops during sleep, so after a
  wake its clock offset is wrong by the sleep's length.
- Our design: when the network thread sees that more than 2 seconds passed between two turns of its loop, it treats it
  as a wake: it drops every row and founds a new session as when left alone (beat continuous), then meets the others
  again as new. That costs a beat jump of at most half a quantum, only after a sleep, and never leaves a wrong offset
  in use.

Clock jumps:
- Only steady clocks are used, for the host clock and for every timer, so a change of the wall-clock time does
  nothing.
- A remote peer's clock is only seen through its session times, which it makes from its own steady clock.

Clock drift between computers:
- Two clocks 20 parts per million apart drift 0.6 ms in 30 seconds; each re-measurement corrects it in one step.
- Our design moves the app's grid to a re-measured offset over one second rather than in one step, so no beat
  jumps; this is local and does not touch the wire.
- On one computer all peers share one clock, so there is no drift and the offsets only move by the measurements'
  noise (a few microseconds).

Two sessions merging:
- Each member of the younger session (or, within half a second, the one with the larger id) measures the other and
  moves; its app's beat moves to the other session's phase by at most half a quantum and takes its tempo.
- Members of the winning session notice nothing: no beat jump, no tempo change.
- If two sessions sit almost exactly half a second apart, two peers can each think the other's session wins and swap;
  each is then alone in the session it moved to, founds a new one, and the next meeting settles it.

A peer joining mid-play:
- The newcomer founds a fresh session at its start, so it is the younger and joins the running session.
- The running peers see no beat jump and no tempo change.
- The newcomer takes the session's tempo and phase; if it was playing, its beat moves by at most half a quantum.
- It does not start or stop because the session is playing; it follows the next start or stop.

A peer that founded a session long ago and was alone all the while is older than any newcomer; when the other program
switches its sync on, the other program joins us.

## What a first version leaves out

IPv6 gateways (multicast group ff12::8080 on port 20808, scope of the interface, and `mep6`):
- Others add one per network adapter that also has IPv4, using its local-scope (fe80::) address, as a fallback when
  IPv4 multicast fails.
- Leaving them out costs only networks that block IPv4 multicast but pass IPv6 multicast, some managed Wi-Fi; peers on
  this computer and on a home network meet over IPv4.

Audio sharing (`aep4`, `aep6` and its own datagrams to those addresses):
- Costs nothing for beat sync; the entries are read past.

The shared song position (`artl`):
- Costs a shared song position, which pattern apps do not have.
- A risk: a peer that shares song positions does not let a start or stop without one move its own transport. If a DAW
  version turns that on, our start and stop may not move that DAW, while the DAW's still move us. Worth watching.

Changing the timeline or play state from the message thread:
- Every change comes from the audio thread in our apps, which is also the most precise place; the UI asks the audio
  thread.

Relaying the entries we do not know:
- We send only our own four entries; a peer that sits between two others that share song positions does not pass
  theirs on. Negligible.

The re-measurement, the 5-second interface rescan and the host-time filter could be left out too, but each is small
and each buys a real thing (sync between computers that lasts longer than a minute, a session that survives a Wi-Fi
reconnect, beats placed within a few microseconds rather than a millisecond of jitter); we keep them.

## The smallest set that works with a DAW on this computer

- IPv4 only, with the loopback gateway; the LAN gateways are the same code with other addresses.
- Announce, reply and leaving, the 250 ms announces with the 50 ms limit, the 5-second time to live and the removal
  after it.
- The four entries `tmln`, `sess`, `stst`, `mep4`, written and read.
- The pong answering pings: without it the other program cannot measure us and never joins our session, so if ours
  is the older, the two stay apart.
- The pinging side, the median offset, and choosing and joining a session.
- The timeline rule (larger anchor beat wins) and the tempo-change rule that makes a change win.
- The play state rule (later time wins) and the quantized start.
- Founding a new session when switched on and when left alone.
- Tempo clamping to 20 to 999 BPM.

## Our design

### Where it lives

In coolsynth-engine, folder `src/sync`, namespace `coolsynth::sync`, built into each app with the rest of the engine.

Its parts:
- `SyncMath.h`: the units, timelines, tempo conversion and clamping, phase, snapping and the quantized start. Header
  only, no state, no JUCE.
- `SyncWire.h/.cpp`: building and reading every datagram into plain structs. No sockets.
- `SyncCore.h/.cpp`: the protocol as a state machine with no sockets and no clock of its own. It is fed events (a
  datagram arrived on gateway g at host time h, the time is now h, the app proposed this, the gateways are now these)
  and returns actions (send these bytes from this socket to this address, wake me at this time, publish this
  snapshot). The peer table, measurements, sessions, timeline and play state rules all live here.
- `SyncSockets.h/.cpp`: the three sockets per gateway on Winsock and on BSD sockets, the interface list
  (GetAdaptersAddresses on Windows, getifaddrs on macOS, addresses that are up and running), and one poll over all
  sockets (WSAPoll or poll).
- `SyncNode.h/.cpp`: the network thread; it owns the sockets and the core and moves bytes and times between them.
- `SyncHandoff.h`: the lock-free handoff between the network thread and the audio and UI threads.
- `BeatSync.h/.cpp`: what an app sees: switch on and off, options, status, and the audio-thread calls.
- `BlockClock.h`: the host-time filter for audio callbacks.
- `tools/beatsync-probe`: a console peer that joins the session and logs every datagram, the session timeline and its
  offset, for interop checks.

Plain sockets rather than JUCE's DatagramSocket: that class binds its multicast membership to the socket's own
address, has no outbound interface, and gives no way to switch off Windows' port-unreachable reports without the raw
handle. JUCE's IPAddress::getAllAddresses also mixes anycast and multicast addresses into its list.

The host clock: juce::Time::getHighResolutionTicks, which is QueryPerformanceCounter on Windows and
mach_absolute_time on macOS, converted with llround(ticks * 1e6 / ticksPerSecond).

Keeping the protocol in a pure state machine makes it testable without a network: tests drive two or more cores
through a simulated network with simulated clocks, losses and delays, fast and repeatably.

### Threads

The message thread switches the beat sync on and off, sets its options and reads its status.
- Switching on starts the network thread; switching off tells it to send its leaving messages and stop, then joins
  it. Nothing else blocks.

One network thread per app does all networking and all protocol work.
- It waits in one poll over every socket, with a timeout up to the core's next wake time and at most 5 ms, so it sees
  the audio thread's proposals within 5 ms without the audio thread making a system call.
- It may allocate; it never touches audio.

The audio thread reads the latest snapshot and posts proposals.
- No locks, no allocation, no system calls; it reads the host clock once per block.

### Handing state to the audio thread

The network thread publishes a snapshot whenever the session changes:
- a snapshot number;
- the session id, the peer count, and whether start/stop sharing is on;
- the session timeline (three 64-bit values) and the clock offset;
- the play state (playing, beat, time) and a play change number that grows when the app should follow a change;
- the number of the last proposal from the audio thread it has applied.

The snapshot is a fixed array of std::atomic<std::int64_t> under a sequence counter (a seqlock): the network thread
makes the counter odd, writes the words, makes it even; a reader reads the counter, the words and the counter again,
and keeps its previous copy if the two counts differ or are odd. There are no data races and any number of readers
(the audio thread, the UI).

The audio thread posts proposals into two single-slot mailboxes, each a few atomic words with a sequence number,
newest wins:
- a grid proposal: a tempo and one point the new grid must pass through (a session beat and its session time); used
  for tempo changes and for the shift of a lone start;
- a play proposal: playing or not, and the host time of the press.

The network thread turns a grid proposal into a timeline with the tempo-change rule against its current timeline, so a
local change always gets a larger anchor beat, even if a remote change came in meanwhile.

The audio thread applies its own proposal at once for its own blocks, and keeps doing so until a snapshot says that
proposal number was applied; then it takes the snapshots again. So a snapshot made before the network thread saw a
change cannot undo it for a block or two.

### The audio thread's calls

A sketch of the API, ours to shape further:

```cpp
namespace coolsynth::sync
{
    struct BlockBeats                // everything at the moment the block's first sample is heard
    {
        bool connected = false;      // another peer is in our session
        int peers = 0;
        double bpm = 120.0;
        double beat = 0.0;           // the app's beat at sample 0
        double beatsPerSample = 0.0; // constant over the block
        double phase = 0.0;          // in [0, quantum)
        bool playing = false;        // the app's transport is running (counts in while beat < 0)
    };

    class BeatSyncAudio              // the audio thread only
    {
    public:
        BlockBeats beginBlock (std::int64_t firstSampleHeardAtHostMicros, int numSamples, double sampleRate);
        void setQuantum (double beats);       // 4 for one bar of 4/4
        void setTempo (double bpm);           // from this block's first sample
        void play (int sampleOffset);         // quantized start; the block's beat goes negative while counting in
        void stop (int sampleOffset);
    };

    class BeatSync                   // the message thread
    {
    public:
        explicit BeatSync (double initialBpm);
        void setEnabled (bool);
        void setStartStopSharing (bool);
        int peerCount() const;               // safe from any thread
        BeatSyncAudio& audio();
    };
}
```

Inside beginBlock:
- Read the snapshot; if it is newer and no own proposal is pending, adopt it, keeping the app's beat continuous at
  this block's start (new app offset = old app beat minus the new session beat, then snapped to the quantum).
- If the snapshot's play change number moved and start/stop sharing is on, follow it: a start sets the app offset
  from the quantized start, a stop stops at the host time of its beat (now, if that is past).
- Compute the session beat at the block's first sample (host time plus offset, through the timeline), add the app
  offset, and return it with beats per sample.

setTempo, play and stop change the audio thread's own state for this block at once and post a proposal.

### Following the transport sample-accurately

The time to give beginBlock is when the block's first sample comes out of the speakers, not when the callback runs.
- First sample heard = host time of the callback, filtered, plus the output latency.
- The output latency is the device's output latency (AudioIODevice::getOutputLatencyInSamples, read again whenever the
  device starts, divided by the sample rate) plus any latency the app's own processing adds.
- Without it, two peers with different output latencies sound steadily apart by the difference, even with perfect
  sync.

The filter: callbacks do not run at perfectly even times, so the host time read in a callback jitters by up to a
millisecond against the sample clock.
- BlockClock keeps the last 512 pairs (samples rendered so far, host time at the callback) in a fixed ring and fits a
  straight line through them; the host time of a block is the line's value at its sample count.
- It works in values relative to its first pair, so doubles keep full precision, and it restarts when the device
  restarts, the sample rate changes or a gap of more than 100 ms shows a dropout.
- Its slope is the real number of microseconds per sample; beatsPerSample uses it rather than the nominal rate.
- On macOS, CoreAudio passes the host time with each buffer (JUCE's AudioIODeviceCallbackContext::hostTimeNs); that
  replaces the filtered value there. On Windows JUCE passes none, so the filter is needed.

Placing events: a step on beat x falls in this block if beat <= x < beat + numSamples * beatsPerSample, at sample
round((x - beat) / beatsPerSample).
- Successive blocks are re-anchored from the filtered clock, so a block's start beat can differ from the previous
  block's end by a few microseconds. When the difference is under a millisecond's worth, the app takes the previous end
  as this start, so no step is played twice or skipped.

### In the three apps

The beat sync runs in the standalone apps only; in a DAW the DAW's own transport rules (a DAW that speaks the protocol
takes part itself).

Each app gets a switch (beat sync on or off), a start/stop sharing switch and a peer count; their place on the panels
waits for the real UI.

The apps already follow a DAW's play head (tempo, beat position, playing). The beat sync feeds the same path through
a small juce::AudioPlayHead of our own that returns the BlockBeats of the current block as PositionInfo: tempo, PPQ
position = beat, playing = playing and beat >= 0, bars of 4/4 from beat 0. So the sequencers, arps and synced LFOs
need no new code.

Play and stop on the panel and the apps' tempo control go through BeatSyncAudio when the sync is on.

The quantum: 4 beats (one bar) to start with, matching the patterns of the 303 and the 808.

The three apps on one computer are three peers; they meet over the loopback gateway like any other program.

## Test plan

### Unit tests of the maths and the wire

Tempo:
- 120 BPM encodes as 500000, `00 00 00 00 00 07 A1 20`; 128 as 468750, `00 00 00 00 00 07 27 0E`; 20 as 3000000.
- 999 BPM gives 60060, reads back clamped to 999 and encodes as 60060 again; 1000 BPM clamps to 999; 10 BPM to 20.
- 0 and -5 microseconds per beat make the datagram invalid.

Beats and times:
- 16.5 beats is 16500000, `00 00 00 00 00 FB C5 20`; session time -7 is `FF FF FF FF FF FF FF F9`.
- Timeline (500000, 2000000, 1000000): beat 3500000 at 1750000, time 2500000 for beat 5000000.
- Beat to time to beat round-trips within one micro-beat over beats from -5 to 10^8, at 20, 123, 128 and 999 BPM.

Phase and quantum:
- phase(9, 4) = 1, phase(-1, 4) = 3, phase(-4, 4) = 0, phase(x, 0) = 0, in micro-beats.
- snap with quantum 4: 0, 1 and -1 micro-beats give 0; 2 beats gives 0; -2 beats gives -4; 2 beats plus one
  micro-beat gives 4; 5.3 gives 4; -2.5 gives -4.
- The app-beat example: session beat 2.5, free offset 11, app beat 14.5.
- Quantized start: B = 13.7, q = 4 gives S = 16; B = 16 exactly gives 16.
- The tempo-change example gives (428571, 12691356, 12345678) and is continuous at 12345678; a second change a moment
  later gets an anchor beat at least one beat higher.

The wire:
- The 107-byte announce of "Byte examples", built and read back field by field; the same as a reply with byte 8 = 2.
- The leaving message, 20 bytes.
- The ping (25 bytes), pong (57) and second ping (41) of the examples.
- Reading: an unknown entry is skipped; an entry whose size runs past the end drops the datagram; a `sess` of 4 bytes
  drops it; a group of 1 is ignored; a wrong magic is ignored; 19 bytes are ignored; a datagram with our own id is
  ignored; an announce without `tmln` or `sess` is ignored.
- A ping whose payload is 33 bytes gets no pong; a pong echoes a 32-byte ping payload byte for byte.
- Building anything of 512 bytes or more fails.

Measurement, on the core with a simulated peer:
- The worked example gives estimates 4000000, 4000005 and 4000005.
- With a fixed offset and random one-way delays of 0 to 2 ms, the result is within 100 microseconds of the true
  offset, and the run ends after more than 100 estimates.
- With every pong lost, the measurement fails at the sixth timeout (about 300 ms).
- A pong with another session id fails the measurement.
- The median of an even count is the mean of the middle two.

Sessions, on the core:
- Other session older by 0.6 s: join; by 0.4 s with a smaller id: join; by 0.4 s with a larger id: stay; younger by
  0.6 s: stay; exactly 0.5 s older: stay.
- On joining: our app beat continuous, then within half a quantum; play state not applied; an announce goes out.
- Timeline rule: a larger anchor beat replaces ours and is announced; an equal one does not.
- Play state rule: a later time replaces ours, an earlier one does not; with start/stop sharing off it is relayed
  but not applied.
- Last peer gone: a new peer id and session id, the app beat and phase continuous.

The handoff and the audio thread:
- beginBlock, setTempo, play and stop run under the engine's audio-thread allocation alarm with no allocation.
- A seqlock reader racing a writer in a stress test never sees a torn snapshot.
- BlockClock recovers a known line from callback times with 1 ms of jitter to within 20 microseconds.

The engine's test runner needs each test in its own UnitTest class with its objects made by std::make_unique, as the
other engine tests do.

### Two of our peers in one process

Two BeatSync nodes on the loopback gateway, real sockets, one process:
- They find each other within a second, and the one switched on second joins the first.
- A tempo change on either reaches the other within 100 ms; their session timelines become identical and their grids,
  in host time, agree within 50 microseconds.
- With start/stop sharing on in both, a start on one starts the other on the same quantum boundary; with it off in
  one, that one does not follow but still relays.
- Switching one off: the other's peer count falls to 0 within 100 ms and it founds a new session with a continuous
  beat.
- One stops answering without a leaving message (its sockets closed): it is gone from the other after 5 to 7
  seconds.
- Two pairs, started a second apart, merged by opening a path between them: the younger pair moves, the older pair's
  beats do not jump.

### With a DAW on this computer that speaks the protocol

The DAW's own sync switch and its start/stop switch on, its quantization at one bar; our app at quantum 4; the probe
logging.

Discovery:
- Both show one peer within a second of the second one switching on.

Tempo both ways:
- The DAW at 120, set to 133.5: our app shows 133.5 within 100 ms and its timeline holds 449438 microseconds per beat.
- Our app set to 97: the DAW shows 97.
- The DAW alone at 130, our app set to 100 then switched on: both at 130 (joining does not change a session's tempo).
- Our app playing at 110 with no session, switched on: its tempo and beat do not change.
- The DAW at 20 and at 999: our app follows exactly.

Beat alignment, measured:
- The probe logs the session timeline as both peers announce it; our app's published grid and the DAW's announced
  grid must give the same session beat at the same moment within 100 microseconds.
- Then by sound: both play a click on every beat, our app's output and the DAW's output looped by cable from the audio
  interface's outputs back into two of its inputs and recorded together; the onsets must agree within 3 ms, over a
  minute, at two buffer sizes, and again after changing our app's buffer size while playing.

Start/stop both ways:
- The DAW starts: our app starts at its next bar line, on the DAW's downbeat; the DAW stops: our app stops.
- Our app starts: the DAW starts at its next bar line; our app stops: the DAW stops.
- Our app with start/stop sharing off: the DAW's start does not start it, and its own start does not start the DAW.
- The DAW playing before our app joins: our app joins stopped and stays stopped until the next start or stop.

A peer leaving:
- The DAW's sync switched off: our peer count goes to 0 within a second; our tempo and beat carry on without a jump
  (the probe and our app's log show a continuous beat).
- The DAW's process ended from Task Manager: our peer count goes to 0 after about 6 seconds, again without a jump.
- The DAW's sync switched on again: it joins our session, which is older; our beat does not jump.

The three apps and the DAW together: all four in one session, tempo and starts from each one in turn.

The computer put to sleep for a minute with our app and the DAW synced: after waking, one session again within about
7 seconds, at the tempo it had.

The probe on this computer, Wi-Fi switched off and on during a session: the loopback session goes on untouched.

## Open questions

- Whether the beat sync is on by default in the standalone apps, and whether start/stop sharing is.
- Whether the quantum stays at 4 or follows something in each app (the 303's and 808's pattern length, the arp's).
- What MIDI clock in does while the beat sync has peers: ignored, or the beat sync off while a clock arrives.
- Where the switches and the peer count go, with the real UI.
- On the Mac: whether binding port 20808 alongside other programs needs SO_REUSEPORT on our side only or fails when
  another program bound it without; and the sleep behaviour of mach_absolute_time. Both for docs/design/MACOS.md.

## Sources

The protocol facts above are our own reading and are checked by the tests against a DAW that speaks the protocol.

Operating-system facts:
- Winsock IPPROTO_IP socket options, on IP_MULTICAST_LOOP applying to the receiving socket on Windows:
  https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
- Winsock IOCTLs, on SIO_UDP_CONNRESET: https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
- Acquiring high-resolution time stamps, on QueryPerformanceCounter counting through sleep:
  https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps

JUCE, in this repository (`external/JUCE`, 9.0.5):
- `modules/juce_core/network/juce_Socket.h` and `juce_Socket.cpp`: DatagramSocket's multicast join uses the bound
  address as its interface.
- `modules/juce_core/native/juce_Network_windows.cpp`: IPAddress::findAllAddresses adds anycast and multicast
  addresses.
- `modules/juce_audio_devices/audio_io/juce_AudioIODevice.h` and `native/juce_CoreAudio_mac.cpp`: hostTimeNs is
  filled on CoreAudio only.
