// Talks to the Windows host (WebView2). In a normal browser (npm run dev) a small simulator takes its place,
// so the screen can still be designed without the phone or the engine.

export type EngineStatus = {
  state: string // disconnected | discovering | connecting | handshaking | configuring | streaming | reconnecting | failed
  session: number
  camera: boolean // camera data is arriving right now
  mic: boolean // audio data is arriving right now
  wantCamera: boolean
  wantMic: boolean
  fps: number
  device: string // e.g. "Google Pixel 8 Pro" ("" until the phone says hello)
}

export type HostState = {
  version: string
  engineFound: boolean
  running: boolean
  status: EngineStatus | null
  error: string | null // missing_engine | start_failed | not_admin | camera_init | exited | driver_failed | ...
  settings: { camera: boolean; mic: boolean }
  driver: { installed: boolean; busy: boolean }
}

export type LogLine = { t: string; text: string }

export type HostMessage = ({ type: 'state' } & HostState) | { type: 'logs'; lines: LogLine[] }

export type Command =
  | { cmd: 'ready' }
  | { cmd: 'start' }
  | { cmd: 'stop' }
  | { cmd: 'restart' }
  | { cmd: 'setStreams'; camera: boolean; mic: boolean }
  | { cmd: 'installDriver' }
  | { cmd: 'uninstallDriver' }

export interface Host {
  isNative: boolean
  send(command: Command): void
  subscribe(handler: (message: HostMessage) => void): () => void
}

type WebViewApi = {
  postMessage(message: unknown): void
  addEventListener(type: 'message', listener: (event: MessageEvent) => void): void
  removeEventListener(type: 'message', listener: (event: MessageEvent) => void): void
}

function nativeWebView(): WebViewApi | undefined {
  return (window as unknown as { chrome?: { webview?: WebViewApi } }).chrome?.webview
}

export function createHost(): Host {
  const wv = nativeWebView()
  if (!wv) return createSimulator()
  return {
    isNative: true,
    send: (command) => wv.postMessage(command),
    subscribe(handler) {
      const listener = (event: MessageEvent) => {
        const data = typeof event.data === 'string' ? safeParse(event.data) : event.data
        if (data && typeof data === 'object' && 'type' in data) handler(data as HostMessage)
      }
      wv.addEventListener('message', listener)
      return () => wv.removeEventListener('message', listener)
    },
  }
}

function safeParse(text: string): unknown {
  try {
    return JSON.parse(text)
  } catch {
    return null
  }
}

// ---------------------------------------------------------------------------------------------
// Browser-only simulator (never used inside the real app).
function createSimulator(): Host {
  const handlers = new Set<(m: HostMessage) => void>()
  let state: HostState = {
    version: 'dev',
    engineFound: true,
    running: false,
    status: null,
    error: null,
    settings: { camera: true, mic: true },
    driver: { installed: true, busy: false },
  }
  const timers: number[] = []
  const emit = () => handlers.forEach((h) => h({ type: 'state', ...state }))
  const log = (text: string) => {
    const t = new Date().toTimeString().slice(0, 8) + '.000'
    handlers.forEach((h) => h({ type: 'logs', lines: [{ t, text }] }))
  }
  const status = (over: Partial<EngineStatus>): EngineStatus => ({
    state: 'connecting', session: 1, camera: false, mic: false,
    wantCamera: state.settings.camera, wantMic: state.settings.mic, fps: 0, device: '', ...over,
  })
  const later = (ms: number, fn: () => void) => timers.push(window.setTimeout(fn, ms))
  const clear = () => timers.splice(0).forEach((id) => window.clearTimeout(id))
  const refreshStream = () => {
    if (state.status?.state !== 'streaming') return
    const { camera, mic } = state.settings
    state = { ...state, status: status({ state: 'streaming', device: 'Google Pixel 8 Pro', camera, mic, fps: camera ? 29.8 : 0 }) }
  }

  return {
    isNative: false,
    send(c) {
      switch (c.cmd) {
        case 'ready': emit(); break
        case 'start':
        case 'restart':
          clear()
          state = { ...state, running: true, error: null, status: status({}) }
          log('[sim] A iniciar o motor...')
          emit()
          later(1200, () => { state = { ...state, status: status({ state: 'streaming', device: 'Google Pixel 8 Pro' }) }; log('[sim] Telemóvel ligado'); refreshStream(); emit() })
          break
        case 'stop':
          clear()
          state = { ...state, running: false, status: null }
          log('[sim] Motor parado')
          emit()
          break
        case 'setStreams':
          state = { ...state, settings: { camera: c.camera, mic: c.mic } }
          refreshStream()
          emit()
          break
        case 'installDriver':
        case 'uninstallDriver':
          state = { ...state, driver: { installed: c.cmd === 'installDriver', busy: false } }
          emit()
          break
      }
    },
    subscribe(handler) {
      handlers.add(handler)
      return () => { handlers.delete(handler) }
    },
  }
}
