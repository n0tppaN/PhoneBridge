import test from 'node:test'
import assert from 'node:assert/strict'
import { classify, headline, logLevel, errorText } from './phase.ts'
import type { HostState, EngineStatus } from './bridge.ts'

const st = (over: Partial<EngineStatus> = {}): EngineStatus => ({
  state: 'streaming', session: 1, camera: false, mic: false, wantCamera: true, wantMic: true, fps: 0, device: 'Google Pixel 8 Pro', ...over,
})
const host = (over: Partial<HostState> = {}): HostState => ({
  version: 't', engineFound: true, running: true, status: null, error: null,
  settings: { camera: true, mic: true }, driver: { installed: true, busy: false }, ...over,
})

test('classify', () => {
  assert.equal(classify(host({ running: false })), 'stopped')
  assert.equal(classify(host()), 'searching')
  assert.equal(classify(host({ status: st({ state: 'connecting' }) })), 'searching')
  assert.equal(classify(host({ status: st() })), 'ready')
  assert.equal(classify(host({ status: st({ camera: true }) })), 'streaming')
  assert.equal(classify(host({ status: st({ mic: true }) })), 'streaming')
  assert.equal(classify(host({ status: st({ state: 'failed' }) })), 'error')
  assert.equal(classify(host({ running: false, error: 'exited' })), 'error')
  assert.equal(classify(host({ status: st({ camera: true }), error: 'camera_init' })), 'error')
})

test('headline', () => {
  const live = host({ status: st({ camera: true, mic: true, fps: 29.8 }) })
  assert.equal(headline(live, 'streaming').title, 'A transmitir')
  assert.match(headline(live, 'streaming').subtitle, /^Câmara e microfone em direto · 30 fps/)
  const mic = host({ status: st({ mic: true }) })
  assert.match(headline(mic, 'streaming').subtitle, /^Microfone em direto\./)
  const none = host({ status: st(), settings: { camera: false, mic: false } })
  assert.match(headline(none, 'ready').subtitle, /Ative a câmara/)
  assert.match(headline(host({ error: 'not_admin' }), 'error').subtitle, /administrador/)
  assert.match(errorText('whatever'), /whatever/)
})

test('logLevel', () => {
  assert.equal(logLevel('[VCam] MFCreateVirtualCamera failed 0x80004005'), 'ERR')
  assert.equal(logLevel('[VirtualCamera] Camera (PhoneBridge) started.'), 'OK')
  assert.equal(logLevel('[ConnectionManager] Status: Disconnected. Reconnecting in 5 seconds...'), 'WAIT')
  assert.equal(logLevel('[ConnectionManager] Received packet type=0x3'), 'INFO')
})
