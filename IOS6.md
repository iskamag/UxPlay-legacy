# iOS 5/6 mirroring mode

The `-ios6` option enables the pre-iOS 9 AirPlay mirroring protocol while
leaving upstream UxPlay behavior unchanged by default. It adds:

- the fixed TCP 7100 service with persistent `/stream.xml`, `/fp-setup`, and
  `/stream` handling;
- direct FairPlay-keyed AES-CTR H.264 decryption for 128-byte legacy mirror
  packets;
- RTSP `ANNOUNCE`/SDP and bodyless `SETUP` support for AAC-ELD audio;
- legacy NTPv4 synchronization between client UDP 7010 and server UDP 7011.

## Build

On this iOS-development workstation, use the host compiler explicitly so the
Theos cross-compiler in the interactive shell does not leak into CMake:

```sh
env PATH=/usr/local/bin:/usr/bin:/bin \
  CC=/usr/bin/gcc CXX=/usr/bin/g++ AR=/usr/bin/ar \
  AS=/usr/bin/as LD=/usr/bin/ld \
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
env PATH=/usr/local/bin:/usr/bin:/bin cmake --build build -j4
```

## Run and record

Stop any packaged UxPlay instance first, then run:

```sh
./build/uxplay -ios6 -n OniCapture -mp4 onislash -fps 30
```

`-ios6` selects the Apple TV 5.x fixed ports automatically: TCP 7000 and 7100,
and UDP 6000, 6001, and 7011. The client provides its NTP clock on UDP 7010.
Allow those ports through a host firewall if one is active.

For a protocol trace, add `-d 1` and capture only the iOS device:

```sh
./build/uxplay -ios6 -n OniCapture -mp4 onislash -fps 30 -d 1 \
  2>&1 | tee ios6-airplay.log
sudo tcpdump -i any -s0 -w ios6-airplay.pcap 'host <IOS_DEVICE_IP>'
```

The expected connection markers are `GET /stream.xml`, two FairPlay stages,
`ANNOUNCE`, bodyless `SETUP`, and `POST /stream`, followed by H.264 packets.
