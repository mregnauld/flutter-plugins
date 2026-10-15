import 'dart:async';

import 'package:desktop_drop/desktop_drop.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

Future<void> _invokePlatformMethod(MethodCall call) async {
  final codec = const StandardMethodCodec();
  final completer = Completer<ByteData?>();
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .handlePlatformMessage(
    'desktop_drop',
    codec.encodeMethodCall(call),
    completer.complete,
  );
  await completer.future;
}

List<DropEvent> _listen() {
  final events = <DropEvent>[];
  void listener(DropEvent event) => events.add(event);
  DesktopDrop.instance.addRawDropEventListener(listener);
  addTearDown(() => DesktopDrop.instance.removeRawDropEventListener(listener));
  return events;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    DesktopDrop.instance.init();
  });

  tearDown(() async {
    await _invokePlatformMethod(const MethodCall('exited'));
  });

  test('entered carries the hover paths to enter and update events', () async {
    final events = _listen();

    await _invokePlatformMethod(const MethodCall('entered', [
      1.0,
      2.0,
      ['/videos/b.mp4', '/videos/a.mp4'],
    ]));
    await _invokePlatformMethod(const MethodCall('updated', [3.0, 4.0]));

    final enter = events[0] as DropEnterEvent;
    expect(enter.location, const Offset(1.0, 2.0));
    expect(enter.paths, ['/videos/b.mp4', '/videos/a.mp4']);
    final update = events[1] as DropUpdateEvent;
    expect(update.location, const Offset(3.0, 4.0));
    expect(update.paths, ['/videos/b.mp4', '/videos/a.mp4']);
  });

  test('entered with an empty list means no file', () async {
    final events = _listen();

    await _invokePlatformMethod(const MethodCall('entered', [1.0, 2.0, []]));

    expect((events.single as DropEnterEvent).paths, isEmpty);
  });

  test('entered without paths keeps them unknown', () async {
    final events = _listen();

    await _invokePlatformMethod(const MethodCall('entered', [1.0, 2.0]));

    expect((events.single as DropEnterEvent).paths, isNull);
  });

  test('exited forgets the hover paths', () async {
    final events = _listen();

    await _invokePlatformMethod(
        const MethodCall('entered', [1.0, 2.0, ['/videos/a.mp4']]));
    await _invokePlatformMethod(const MethodCall('exited'));
    await _invokePlatformMethod(const MethodCall('entered', [1.0, 2.0]));

    expect((events.last as DropEnterEvent).paths, isNull);
  });

  test('linux hover paths arrive with an update event', () async {
    final events = _listen();

    // On Linux the enter event comes from the first "updated" message; an
    // "entered" without paths stands in for it on any host.
    await _invokePlatformMethod(const MethodCall('entered', [1.0, 2.0]));
    await _invokePlatformMethod(const MethodCall('hoverPaths_linux',
        'file:///videos/a%20b.mp4\r\nfile:///videos/c.mp4\r\n'));
    await _invokePlatformMethod(const MethodCall('updated', [3.0, 4.0]));

    expect(events.map((e) => e.runtimeType),
        [DropEnterEvent, DropUpdateEvent, DropUpdateEvent]);
    expect((events[0] as DropEnterEvent).paths, isNull);
    expect((events[1] as DropUpdateEvent).paths,
        ['/videos/a b.mp4', '/videos/c.mp4']);
    expect((events[2] as DropUpdateEvent).location, const Offset(3.0, 4.0));
    expect((events[2] as DropUpdateEvent).paths,
        ['/videos/a b.mp4', '/videos/c.mp4']);
  });

  test('linux hover paths after the drag ended are ignored', () async {
    final events = _listen();

    await _invokePlatformMethod(
        const MethodCall('hoverPaths_linux', 'file:///videos/a.mp4\r\n'));

    expect(events, isEmpty);
  });
}
