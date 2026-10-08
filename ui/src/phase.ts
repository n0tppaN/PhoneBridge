import type { HostState } from './bridge'

export type Phase = 'stopped' | 'searching' | 'ready' | 'streaming' | 'error'

/** Same five phases as the Android app. "streaming" in the engine = phone connected (handshake done). */
export function classify(s: HostState): Phase {
  if (s.error) return 'error'
  if (!s.running) return 'stopped'
  const st = s.status
  if (!st) return 'searching'
  if (st.state === 'failed') return 'error'
  if (st.state === 'streaming') return st.camera || st.mic ? 'streaming' : 'ready'
  return 'searching'
}

export function errorText(code: string | null): string {
  switch (code) {
    case 'not_admin': return 'Execute o PhoneBridge como administrador.'
    case 'camera_init': return 'Não foi possível criar a câmara virtual. Instale o driver da câmara (interruptor ao lado).'
    case 'exited': return 'O motor de transmissão parou inesperadamente. Veja a consola.'
    case 'missing_engine': return 'Falta o ficheiro phonebridge_service.exe junto do PhoneBridge.exe.'
    case 'start_failed': return 'Não foi possível iniciar o motor de transmissão.'
    case 'driver_failed': return 'A instalação do driver da câmara falhou. Veja a consola.'
    case null: return 'Ocorreu um erro desconhecido.'
    default: return 'Erro: ' + code
  }
}

export function headline(s: HostState, phase: Phase): { title: string; subtitle: string } {
  const st = s.status
  switch (phase) {
    case 'stopped':
      return { title: 'Parado', subtitle: 'Clique em Iniciar para usar o telemóvel como câmara e microfone.' }
    case 'searching':
      return {
        title: 'A procurar o telemóvel',
        subtitle: 'Ligue o cabo USB, ative a depuração USB e abra a app PhoneBridge no telemóvel.',
      }
    case 'ready':
      return {
        title: 'Telemóvel ligado',
        subtitle: s.settings.camera || s.settings.mic
          ? 'A iniciar a transmissão...'
          : 'Ative a câmara ou o microfone para começar a transmitir.',
      }
    case 'streaming': {
      const cam = !!st?.camera
      const mic = !!st?.mic
      let what = cam && mic ? 'Câmara e microfone em direto' : cam ? 'Câmara em direto' : 'Microfone em direto'
      if (cam && st && st.fps > 0) what += ` · ${Math.round(st.fps)} fps`
      return { title: 'A transmitir', subtitle: what + '. Escolha PhoneBridge na lista de câmaras da sua aplicação.' }
    }
    default:
      return { title: 'Algo correu mal', subtitle: errorText(s.error) }
  }
}

export type LogLevel = 'ERR' | 'OK' | 'WAIT' | 'INFO'

export function logLevel(text: string): LogLevel {
  const t = text.toLowerCase()
  if (/\b(error|erro|failed|fail|falhou)\b/.test(t)) return 'ERR'
  if (/(started|success|ready|connected|completed|published|decoded|installed)/.test(t) && !/disconnected/.test(t)) return 'OK'
  if (/(waiting|retrying|disconnected|reconnecting|warn|searching)/.test(t)) return 'WAIT'
  return 'INFO'
}
