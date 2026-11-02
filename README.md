# Networks-Project-2

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

## Packet format

Both sides use the `tcp_packet` struct from `packet.h`. Sequence numbers are byte
offsets into the file, so a packet's `seqno` is where its payload starts. An ACK
carries `ackno = seqno + data_size` of the packet being acknowledged, which is the
byte offset right after it. A packet with `data_size == 0` marks the end of the file.

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
