# Networks-Project-2

Yousef Al-Jazzazi and Mohammad Ashar Shaikh

Selective Repeat reliable data transfer over UDP.

## Build and run

```
cd starter/build
make
```

Receiver (run this first):

```
./rdt_receiver 5454 received_file.txt
```

Sender, inside the MahiMahi shell:

```
mm-delay 5 mm-loss uplink 0.2 mm-loss downlink 0.5
./rdt_sender $MAHIMAHI_BASE 5454 sample_file.txt
```

The sender reads from `starter/build/sampledata/` and the receiver writes into
`starter/build/receiveddata/`.

## Key design decisions

- An ACK names one packet by its own sequence number, rather than acknowledging
  everything up to a point. Selective Repeat needs to know which of the packets in
  flight arrived, and a cumulative ACK cannot say that.
- The sender window and the receiver buffer are both fixed arrays of 10 entries, so
  there is no dynamic allocation to manage on either side and the two sides cannot
  disagree about how much can be outstanding.
- Each unacknowledged packet carries its own `send_time`, which gives it its own
  logical timer. The starter used a single SIGALRM timer, which only works when
  there is one packet in flight.
- The sender's main loop never blocks on `recvfrom`. It waits on `select` with a
  10 ms timeout so that sending, handling ACKs and checking timers all keep running.
- The timeout is a fixed 150 ms rather than an estimated round trip time, which is
  what the project asks for and is simpler to reason about.
- The receiver writes with `pwrite` using the sequence number as the file offset, so
  it never has to track a separate write position.
- Out-of-order packets are ACKed, not just buffered. If they were only buffered the
  sender would keep resending packets the receiver already holds.
- The receiver waits a short while after the transfer instead of exiting at once, so
  a lost ACK does not leave the sender retransmitting into a void.

## Packet format

Both sides use the `tcp_packet` struct from `packet.h`. Sequence numbers are byte
offsets into the file, so a packet's `seqno` is where its payload starts. An ACK
carries `ackno = seqno` of the packet being acknowledged. A packet with
`data_size == 0` marks the end of the file.

Each ACK names one exact packet, which is what Selective Repeat needs: the sender
has up to 10 packets in flight and has to know which one of them got through. An
earlier version sent back `seqno + data_size` instead, but then the last data
packet and the end of file packet produce the same number (the EOF packet starts
exactly where the last data packet ends), so one ACK appeared to acknowledge both
and the sender could finish while a packet was still missing.

## Receiver: buffering and in-order delivery

The receiver keeps `expected_seqno`, the offset of the next byte it still needs to
write. It also keeps an array of 10 buffer slots, the same size as the sender's
window, since the sender can never have more than 10 packets in flight. Each slot
holds one out-of-order packet: its sequence number, its length, and a copy of its
payload.

Every packet that arrives falls into one of three cases:

1. `seqno == expected_seqno`. This is the packet we were waiting for, so it gets
   written to the file right away and `expected_seqno` moves forward by its size.
   Writing it may have filled a gap, so the receiver then walks its buffer looking
   for a packet starting at the new `expected_seqno`, writes that one too, and keeps
   going until no buffered packet lines up. That is how a run of packets that
   arrived early gets flushed out in the right order.

2. `seqno > expected_seqno`. A packet arrived early, so something before it was lost
   or delayed. It is copied into a free buffer slot instead of being thrown away. If
   it is already in the buffer it is ignored, and if every slot is full it is dropped
   without an ACK so the sender retransmits it later.

3. `seqno < expected_seqno`. This is an old packet that was already written, which
   usually means an earlier ACK was lost. Nothing is written again.

In cases 1 and 2 the receiver always replies with an ACK, even for a packet that is
out of order, because under Selective Repeat the sender needs to mark that specific
packet as delivered. Case 3 is also ACKed so the sender stops resending it.

The end of file packet is handled by the same rules. If it shows up while a gap is
still open, it sits in the buffer like any other packet and the transfer only
finishes once `expected_seqno` reaches it, so a late packet is never lost just
because the EOF overtook it.

The file is written with `pwrite` using the sequence number as the offset, so each
payload lands in the right place in the output file.

Once the end of file packet has been delivered the receiver does not quit straight
away. Some of its ACKs may have been lost, which means the sender could still be
resending packets we already have. If the receiver disappeared at that moment those
retransmissions would never be answered and the sender would keep retrying until it
gave up with an error, even though the file was complete. So the receiver stays for
3 more seconds and replies to anything that arrives before shutting down.

## Sender: window, ACKs and timers

The sender keeps an array of 10 slots, each holding one packet it has sent but not
yet had acknowledged, along with the time it was last sent. It fills the window as
long as `next_seqno` is inside `send_base + WND * MAX_DATA_SIZE`, so up to 10
packets are in flight at once instead of one.

Slots are handed out in order with `total_packets_sent % WND`, and the sender also
checks that the slot it is about to use is actually free. That check matters because
the window is measured in bytes while slots are counted per packet: the last packet
of a file is usually shorter than `MAX_DATA_SIZE`, so the byte test on its own would
let an eleventh packet out and overwrite a slot still holding an unacknowledged
packet. That packet would then never be retransmitted and its ACK would match
nothing, leaving the sender stuck.

The main loop does not block on `recvfrom`. It waits on `select` with a 10 ms
timeout so it can do three things in turn: send new packets while there is room in
the window, take in any ACK that arrived, and check its timers.

When an ACK arrives the sender looks for the slot whose `seqno` matches it and marks
that slot acknowledged. Only when the packet sitting at `send_base` is acknowledged
does the window slide, and it then keeps sliding over any following packets that are
already acknowledged. The transfer is finished when the window has slid all the way
to the end of file packet, not simply when that packet gets an ACK, because the EOF
can be acknowledged while an earlier packet is still outstanding.

Every unacknowledged slot carries its own `send_time`, which gives each packet its
own logical timer. If more than 150 ms have passed for a slot, only that packet is
resent and its timer restarts. That is the selective part: a lost packet does not
force everything after it to be sent again.
