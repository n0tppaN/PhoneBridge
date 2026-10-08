import { useEffect, useRef, useState, type ReactNode } from 'react'
import {
  ChevronDown,
  ChevronUp,
  CircleDot,
  Clipboard,
  Gauge,
  Hash,
  Mic,
  MicOff,
  Minimize2,
  MonitorUp,
  PanelBottomClose,
  PanelBottomOpen,
  Play,
  RefreshCw,
  Settings,
  Smartphone,
  Square,
  Trash2,
  Usb,
  Video,
  VideoOff,
  X,
  Expand,
} from 'lucide-react'
import { useBridge } from './useBridge'
import { classify, headline, logLevel, type LogLevel, type Phase } from './phase'
import type { HostState, LogLine } from './bridge'

/* ------------------------------------------------------------------ basic pieces (from the design) */

type ButtonProps = React.ButtonHTMLAttributes<HTMLButtonElement> & {
  children: ReactNode
  variant?: 'ghost' | 'secondary' | 'primary' | 'danger'
  size?: 'sm' | 'md' | 'icon'
}

function Button({ children, className = '', variant = 'ghost', size = 'md', type = 'button', ...props }: ButtonProps) {
  const variants = {
    ghost: 'text-ink-muted hover:bg-surface-high hover:text-ink',
    secondary: 'border border-stroke bg-surface-high text-ink hover:border-stroke-strong hover:bg-surface-hover',
    primary: 'bg-accent text-white shadow-accent hover:bg-accent-strong disabled:cursor-not-allowed disabled:opacity-50',
    danger: 'text-ink-muted hover:bg-danger/15 hover:text-danger',
  }
  const sizes = {
    sm: 'h-8 gap-2 px-3 text-xs',
    md: 'h-10 gap-2 px-4 text-sm',
    icon: 'size-9 justify-center',
  }
  return (
    <button
      type={type}
      className={`inline-flex shrink-0 items-center rounded-md font-medium transition-all focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent/70 disabled:opacity-50 ${variants[variant]} ${sizes[size]} ${className}`}
      {...props}
    >
      {children}
    </button>
  )
}

function Toggle({
  checked,
  onChange,
  label,
  disabled = false,
  thumbClassName = '',
}: {
  checked: boolean
  onChange: () => void
  label: string
  disabled?: boolean
  thumbClassName?: string
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      disabled={disabled}
      onClick={onChange}
      className={`relative h-5 w-9 shrink-0 rounded-full transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent disabled:opacity-50 ${
        checked ? 'bg-accent' : 'bg-stroke-strong'
      }`}
    >
      <span
        className={`absolute top-0.5 size-4 rounded-full bg-white shadow-sm transition-transform ${thumbClassName} ${
          checked ? 'translate-x-4' : 'translate-x-0.5'
        }`}
      />
    </button>
  )
}

type Tone = 'success' | 'warning' | 'danger' | 'accent' | 'muted'
// Full class names on purpose: Tailwind only generates classes it can find as plain text.
const TONE: Record<Tone, { box: string; dot: string; text: string }> = {
  success: { box: 'border-success/30 bg-success/10 text-success', dot: 'bg-success', text: 'text-success' },
  warning: { box: 'border-warning/30 bg-warning/10 text-warning', dot: 'bg-warning', text: 'text-warning' },
  danger: { box: 'border-danger/30 bg-danger/10 text-danger', dot: 'bg-danger', text: 'text-danger' },
  accent: { box: 'border-accent/30 bg-accent/10 text-accent-light', dot: 'bg-accent', text: 'text-accent-light' },
  muted: { box: 'border-stroke-strong bg-surface-high text-ink-muted', dot: 'bg-ink-dim', text: 'text-ink-muted' },
}

const CHIP: Record<Tone, string> = {
  success: 'bg-success/10 text-success',
  warning: 'bg-warning/10 text-warning',
  danger: 'bg-danger/10 text-danger',
  accent: 'bg-accent/10 text-accent-light',
  muted: 'bg-surface-high text-ink-muted',
}

const BADGE: Record<Phase, { label: string; tone: Tone }> = {
  stopped: { label: 'PARADO', tone: 'muted' },
  searching: { label: 'A PROCURAR', tone: 'warning' },
  ready: { label: 'TELEMÓVEL LIGADO', tone: 'accent' },
  streaming: { label: 'A TRANSMITIR', tone: 'success' },
  error: { label: 'ERRO', tone: 'danger' },
}

function StatusBadge({ phase }: { phase: Phase }) {
  const { label, tone } = BADGE[phase]
  const t = TONE[tone]
  return (
    <div
      className={`inline-flex h-7 items-center gap-2 rounded-full border px-3 text-[11px] font-semibold tracking-[0.12em] ${t.box}`}
      role="status"
    >
      <span className="relative flex size-2">
        {phase === 'streaming' && (
          <span className="absolute size-full animate-ping rounded-full bg-success opacity-50" />
        )}
        <span className={`relative size-2 rounded-full ${t.dot}`} />
      </span>
      {label}
    </div>
  )
}

function Header({ phase, native }: { phase: Phase; native: boolean }) {
  return (
    <header className="flex h-14 shrink-0 items-center border-b border-stroke bg-base/95 px-4 select-none">
      <div className="flex min-w-0 flex-1 items-center gap-3">
        <div className="logo-mark grid size-8 place-items-center rounded-md">
          <Smartphone className="size-4 text-white" strokeWidth={2.25} />
        </div>
        <div className="flex items-baseline gap-2">
          <span className="text-sm font-semibold tracking-tight text-ink">PhoneBridge</span>
          <span className="hidden text-[10px] font-medium tracking-widest text-ink-dim sm:inline">DESKTOP</span>
        </div>
        <div className="mx-2 hidden h-5 w-px bg-stroke sm:block" />
        <StatusBadge phase={phase} />
      </div>

      {/* Inside the real app the Windows title bar already has these buttons. */}
      {!native && (
        <div className="flex items-center gap-0.5">
          <Button size="icon" aria-label="Definições">
            <Settings className="size-4" />
          </Button>
          <div className="mx-2 h-5 w-px bg-stroke" />
          <Button size="icon" aria-label="Minimizar janela">
            <Minimize2 className="size-4" />
          </Button>
          <Button size="icon" aria-label="Maximizar janela">
            <Square className="size-3.5" />
          </Button>
          <Button size="icon" variant="danger" aria-label="Fechar janela">
            <X className="size-4" />
          </Button>
        </div>
      )}
    </header>
  )
}

function Metric({ icon, label, value, accent = false }: { icon: ReactNode; label: string; value: string; accent?: boolean }) {
  return (
    <div className="flex items-center gap-2.5 border-r border-white/10 px-3 last:border-0">
      <span className={accent ? 'text-success' : 'text-ink-dim'}>{icon}</span>
      <div>
        <div className="text-[9px] font-semibold tracking-wider text-ink-dim uppercase">{label}</div>
        <div className={`text-xs font-semibold tabular-nums ${accent ? 'text-success' : 'text-ink'}`}>{value}</div>
      </div>
    </div>
  )
}

/* ------------------------------------------------------------------ left: stream panel */

function SourceToggle({
  on,
  label,
  onIcon,
  offIcon,
  onClick,
}: {
  on: boolean
  label: string
  onIcon: ReactNode
  offIcon: ReactNode
  onClick: () => void
}) {
  return (
    <Button
      variant={on ? 'secondary' : 'ghost'}
      size="sm"
      onClick={onClick}
      role="switch"
      aria-checked={on}
      aria-label={`Transmitir ${label.toLowerCase()}`}
    >
      {on ? onIcon : offIcon}
      {label}
      <span
        aria-hidden="true"
        className={`relative h-4 w-7 rounded-full transition-colors ${on ? 'bg-accent' : 'bg-stroke-strong'}`}
      >
        <span
          className={`absolute top-0.5 -ml-[13px] -mr-[13px] size-3 rounded-full bg-white transition-transform ${
            on ? 'translate-x-3.5' : 'translate-x-0.5'
          }`}
        />
      </span>
    </Button>
  )
}

function StreamPanel({
  state,
  phase,
  onToggleCamera,
  onToggleMic,
}: {
  state: HostState
  phase: Phase
  onToggleCamera: () => void
  onToggleMic: () => void
}) {
  const st = state.status
  const { title, subtitle } = headline(state, phase)
  const live = phase === 'streaming'
  const icon =
    phase === 'streaming' ? <Video className="size-8 text-success" /> :
    phase === 'ready' ? <Smartphone className="size-8 text-accent-light" /> :
    phase === 'searching' ? <Usb className="size-8 animate-pulse text-warning" /> :
    phase === 'error' ? <VideoOff className="size-8 text-danger" /> :
    <VideoOff className="size-8 text-ink-dim" />

  return (
    <section className="flex min-h-0 flex-1 flex-col overflow-hidden border border-stroke bg-surface">
      <div className="flex h-11 shrink-0 items-center justify-between border-b border-stroke px-4">
        <div className="flex items-center gap-2">
          <Video className="size-4 text-accent-light" />
          <span className="text-xs font-semibold text-ink">Transmissão</span>
          <span className="text-[10px] text-ink-dim">{st?.device ? `${st.device} · Câmara traseira` : 'Nenhum telemóvel ligado'}</span>
        </div>
      </div>

      <div className="flex min-h-0 flex-1 items-center justify-center bg-base p-3 lg:p-4">
        <div
          className={`preview-stage relative aspect-video max-h-full w-full max-w-5xl overflow-hidden border bg-black transition-all ${
            live ? 'border-success/50 shadow-live' : phase === 'error' ? 'border-danger/40' : 'border-stroke-strong'
          }`}
        >
          <div className="absolute inset-0 grid place-items-center p-6">
            <div className="max-w-md text-center" aria-live="polite">
              <div className="mx-auto mb-3 grid place-items-center">{icon}</div>
              <p className={`text-sm font-semibold ${phase === 'error' ? 'text-danger' : 'text-ink'}`}>{title}</p>
              <p className="mt-1.5 text-xs leading-5 text-ink-muted">{subtitle}</p>
              {live && (
                <p className="mt-3 text-[10px] text-ink-dim">
                  A imagem aparece nas aplicações de vídeo (Câmara do Windows, Discord, OBS). Esta janela não mostra pré-visualização.
                </p>
              )}
            </div>
          </div>

          {state.running && (
            <div className="absolute bottom-3 left-1/2 flex -translate-x-1/2 overflow-hidden rounded-md border border-white/10 bg-black/65 py-2 backdrop-blur-md">
              <Metric
                icon={<Gauge className="size-3.5" />}
                label="Frames"
                value={st && st.fps > 0 ? `${st.fps.toFixed(1)} FPS` : '—'}
                accent={!!st && st.fps > 0}
              />
              <Metric icon={<Expand className="size-3.5" />} label="Formato" value="1080p" />
              <Metric icon={<Hash className="size-3.5" />} label="Sessão" value={st ? `#${st.session}` : '—'} />
            </div>
          )}
        </div>
      </div>

      <div className="flex min-h-14 shrink-0 flex-wrap items-center justify-between gap-3 border-t border-stroke bg-surface px-4 py-2">
        <div className="flex flex-wrap items-center gap-2">
          <SourceToggle
            on={state.settings.camera}
            label="Câmara"
            onIcon={<Video className="size-3.5" />}
            offIcon={<VideoOff className="size-3.5" />}
            onClick={onToggleCamera}
          />
          <SourceToggle
            on={state.settings.mic}
            label="Microfone"
            onIcon={<Mic className="size-3.5" />}
            offIcon={<MicOff className="size-3.5" />}
            onClick={onToggleMic}
          />
        </div>
        {st?.camera && (
          <div className="hidden items-center gap-2 text-[10px] text-ink-dim xl:flex">
            <span className="size-1.5 rounded-full bg-success" />
            A publicar na câmara virtual
          </div>
        )}
      </div>
    </section>
  )
}

/* ------------------------------------------------------------------ right: settings panel */

function SectionTitle({ children, icon, badge }: { children: ReactNode; icon: ReactNode; badge?: string }) {
  return (
    <div className="mb-3 flex items-center gap-2">
      <span className="text-accent-light">{icon}</span>
      <span className="text-[10px] font-semibold tracking-[0.15em] text-ink-muted uppercase">{children}</span>
      {badge && (
        <span className="rounded-sm bg-surface-high px-1.5 py-0.5 text-[9px] font-medium tracking-wider text-ink-dim">
          {badge}
        </span>
      )}
    </div>
  )
}

function SelectField({ label, value, options, disabled }: { label: string; value: string; options: string[]; disabled?: boolean }) {
  return (
    <label className="block">
      <span className="mb-1.5 block text-[11px] font-medium text-ink-muted">{label}</span>
      <span className="relative block">
        <select
          value={value}
          disabled={disabled}
          onChange={() => {}}
          className="h-9 w-full appearance-none rounded-md border border-stroke bg-base px-3 pr-8 text-xs font-medium text-ink outline-none transition-colors hover:border-stroke-strong focus:border-accent disabled:cursor-not-allowed disabled:opacity-60"
        >
          {options.map((option) => (
            <option key={option}>{option}</option>
          ))}
        </select>
        <ChevronDown className="pointer-events-none absolute top-1/2 right-2.5 size-3.5 -translate-y-1/2 text-ink-dim" />
      </span>
    </label>
  )
}

function SettingsPanel({
  state,
  phase,
  showConsole,
  onToggleConsole,
  onStartStop,
  onToggleDriver,
}: {
  state: HostState
  phase: Phase
  showConsole: boolean
  onToggleConsole: () => void
  onStartStop: () => void
  onToggleDriver: () => void
}) {
  const st = state.status
  const phoneLinked = phase === 'ready' || phase === 'streaming'
  const running = state.running
  const connection: { label: string; tone: Tone } = phoneLinked
    ? { label: 'LIGADO', tone: 'success' }
    : running
      ? { label: 'A PROCURAR', tone: 'warning' }
      : { label: 'INATIVO', tone: 'muted' }

  return (
    <aside className="flex min-h-0 flex-col border border-stroke bg-surface">
      <div className="flex-1 overflow-y-auto p-4">
        <SectionTitle icon={<Usb className="size-3.5" />}>Ligação</SectionTitle>
        <div className="mb-5 border border-stroke bg-base p-3">
          <div className="flex items-start justify-between gap-3">
            <div className="flex min-w-0 items-center gap-3">
              <div
                className={`grid size-9 shrink-0 place-items-center rounded-md border ${
                  phoneLinked ? 'border-success/20 bg-success/10' : 'border-stroke bg-surface-high'
                }`}
              >
                <Smartphone className={`size-4 ${phoneLinked ? 'text-success' : 'text-ink-dim'}`} />
              </div>
              <div className="min-w-0">
                <p className="truncate text-xs font-semibold text-ink">
                  {st?.device || (running ? 'A aguardar o telemóvel…' : 'Nenhum telemóvel')}
                </p>
                <p className="mt-0.5 truncate font-mono text-[10px] text-ink-dim">ADB · tcp:27183</p>
              </div>
            </div>
            <span className={`rounded-sm px-2 py-1 text-[9px] font-semibold tracking-wider ${CHIP[connection.tone]}`}>
              {connection.label}
            </span>
          </div>
          <div className="mt-3 grid grid-cols-2 gap-px bg-stroke">
            <div className="flex items-center gap-2 bg-surface px-2.5 py-2">
              <Hash className="size-3 text-accent-light" />
              <span className="text-[10px] text-ink-muted">{st ? `Sessão #${st.session}` : 'Sem sessão'}</span>
            </div>
            <div className="flex items-center gap-2 bg-surface px-2.5 py-2">
              <Gauge className={`size-3 ${st && st.fps > 0 ? 'text-success' : 'text-ink-dim'}`} />
              <span className="text-[10px] text-ink-muted">{st && st.fps > 0 ? `${st.fps.toFixed(1)} FPS` : 'Sem vídeo'}</span>
            </div>
          </div>
        </div>

        <SectionTitle icon={<Settings className="size-3.5" />} badge="FIXO NESTA VERSÃO">
          Configuração do vídeo
        </SectionTitle>
        <div className="space-y-3.5">
          <div className="grid grid-cols-2 gap-3">
            <SelectField label="Resolução" value="1080p · 30 FPS" options={['1080p · 30 FPS']} disabled />
            <SelectField label="Codificador" value="H.264" options={['H.264']} disabled />
          </div>

          <label className="block">
            <span className="mb-1.5 flex items-center justify-between text-[11px] font-medium text-ink-muted">
              <span>Débito alvo</span>
              <span className="font-mono text-accent-light">8.0 Mbps</span>
            </span>
            <input type="range" min="2" max="25" step="0.5" value={8} disabled readOnly className="bitrate-slider w-full" aria-label="Débito alvo (fixo)" />
            <span className="mt-1 flex justify-between text-[9px] text-ink-dim">
              <span>2 Mbps</span>
              <span>25 Mbps</span>
            </span>
          </label>

          <div className="flex items-center justify-between border border-stroke bg-base p-3">
            <div className="flex min-w-0 items-center gap-2.5">
              <MonitorUp className="size-4 shrink-0 text-accent-light" />
              <div className="min-w-0">
                <p className="text-[11px] font-medium text-ink">Fonte de câmara Windows</p>
                <p className="truncate text-[9px] text-ink-dim">
                  {state.driver.busy ? 'A trabalhar… (pode demorar uns segundos)' : state.driver.installed ? 'Media Foundation instalada · OBS/DirectShow experimental' : 'Não instalada · ligue para instalar'}
                </p>
              </div>
            </div>
            <Toggle
              checked={state.driver.installed}
              disabled={state.driver.busy}
              onChange={onToggleDriver}
              label="Instalar ou remover a fonte Media Foundation da câmara"
              thumbClassName="-ml-4 -mr-4"
            />
          </div>

          <div className="flex items-center justify-between border border-stroke bg-base p-3">
            <div className="flex min-w-0 items-center gap-2.5">
              <PanelBottomOpen className="size-4 shrink-0 text-accent-light" />
              <div className="min-w-0">
                <p className="text-[11px] font-medium text-ink">Consola do serviço</p>
                <p className="truncate text-[9px] text-ink-dim">Mostrar o diagnóstico no ecrã</p>
              </div>
            </div>
            <Toggle checked={showConsole} onChange={onToggleConsole} label="Mostrar a consola do serviço" />
          </div>
        </div>
      </div>

      <div className="shrink-0 border-t border-stroke bg-base/60 p-4">
        <Button
          variant={running ? 'secondary' : 'primary'}
          disabled={!state.engineFound || state.driver.busy}
          className={`h-12 w-full justify-center text-xs font-semibold tracking-[0.08em] ${
            running ? 'bridge-stop-button border-danger/40 text-white hover:bg-danger/10' : ''
          }`}
          onClick={onStartStop}
        >
          {running ? <Square className="size-3.5 fill-current" /> : <Play className="size-4 fill-current" />}
          {running ? 'PARAR' : 'INICIAR'}
        </Button>
        <div className="mt-2 flex items-center justify-center gap-1.5 text-[9px] text-ink-dim">
          <CircleDot className="size-2.5" />
          {!state.engineFound
            ? 'Falta o phonebridge_service.exe'
            : phase === 'streaming'
              ? 'A publicar nos dispositivos virtuais'
              : running
                ? 'A aguardar o telemóvel'
                : 'Pronto para publicar nos dispositivos virtuais'}
        </div>
      </div>
    </aside>
  )
}

/* ------------------------------------------------------------------ bottom: console */

const LEVEL_CLASS: Record<LogLevel, string> = {
  ERR: 'text-danger',
  OK: 'text-success',
  WAIT: 'text-warning',
  INFO: 'text-accent-light',
}

function LogTerminal({ logs, running, onClear, onRestart }: { logs: LogLine[]; running: boolean; onClear: () => void; onRestart: () => void }) {
  const [expanded, setExpanded] = useState(true)
  const [copied, setCopied] = useState(false)
  const endRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    endRef.current?.scrollIntoView({ block: 'end' })
  }, [logs, expanded])

  function copyLogs() {
    void navigator.clipboard?.writeText(logs.map((l) => `${l.t} ${logLevel(l.text)} ${l.text}`).join('\n'))
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1200)
  }

  return (
    <footer className="shrink-0 border border-stroke bg-surface">
      <div className="flex h-10 items-center justify-between border-b border-stroke px-3">
        <button
          type="button"
          onClick={() => setExpanded(!expanded)}
          className="flex items-center gap-2 rounded-sm text-xs font-semibold text-ink hover:text-white focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
          aria-expanded={expanded}
        >
          {expanded ? <PanelBottomClose className="size-3.5 text-accent-light" /> : <PanelBottomOpen className="size-3.5 text-accent-light" />}
          Consola do serviço
          <span
            className={`rounded-sm px-1.5 py-0.5 text-[9px] font-medium ${
              running ? 'bg-success/10 text-success' : 'bg-surface-high text-ink-dim'
            }`}
          >
            {running ? 'A CORRER' : 'PARADO'}
          </span>
          {expanded ? <ChevronDown className="size-3 text-ink-dim" /> : <ChevronUp className="size-3 text-ink-dim" />}
        </button>
        <div className="flex items-center gap-1">
          <Button size="sm" onClick={copyLogs} disabled={!logs.length}>
            <Clipboard className="size-3" />
            <span className="hidden sm:inline">{copied ? 'Copiado' : 'Copiar'}</span>
          </Button>
          <Button size="sm" onClick={onClear} disabled={!logs.length}>
            <Trash2 className="size-3" />
            <span className="hidden sm:inline">Limpar</span>
          </Button>
          <Button size="sm" onClick={onRestart} disabled={!running}>
            <RefreshCw className="size-3" />
            <span className="hidden sm:inline">Reiniciar serviço</span>
          </Button>
        </div>
      </div>

      {expanded && (
        <div className="console-scroll h-28 overflow-y-auto bg-console px-4 py-2.5 font-mono text-[10px] leading-5">
          {logs.length ? (
            logs.map((log, index) => {
              const level = logLevel(log.text)
              return (
                <div key={`${log.t}-${index}`} className="grid grid-cols-[5.5rem_2.5rem_1fr] gap-2">
                  <span className="text-ink-dim">{log.t}</span>
                  <span className={LEVEL_CLASS[level]}>{level}</span>
                  <span className="break-all text-ink-muted">{log.text}</span>
                </div>
              )
            })
          ) : (
            <div className="flex h-full items-center justify-center text-ink-dim">Sem mensagens</div>
          )}
          <div ref={endRef} />
        </div>
      )}
    </footer>
  )
}

/* ------------------------------------------------------------------ app */

export default function App() {
  const { state, logs, send, clearLogs, isNative } = useBridge()
  const [showConsole, setShowConsole] = useState(false)
  const phase = classify(state)

  return (
    <main className="flex h-screen min-h-[640px] flex-col overflow-hidden bg-base text-ink">
      <Header phase={phase} native={isNative} />
      <div className="flex min-h-0 flex-1 flex-col gap-3 p-3">
        <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 lg:grid-cols-[minmax(0,1.85fr)_minmax(320px,1fr)]">
          <StreamPanel
            state={state}
            phase={phase}
            onToggleCamera={() => send({ cmd: 'setStreams', camera: !state.settings.camera, mic: state.settings.mic })}
            onToggleMic={() => send({ cmd: 'setStreams', camera: state.settings.camera, mic: !state.settings.mic })}
          />
          <SettingsPanel
            state={state}
            phase={phase}
            showConsole={showConsole}
            onToggleConsole={() => setShowConsole(!showConsole)}
            onStartStop={() => send({ cmd: state.running ? 'stop' : 'start' })}
            onToggleDriver={() => send({ cmd: state.driver.installed ? 'uninstallDriver' : 'installDriver' })}
          />
        </div>
        {showConsole && <LogTerminal logs={logs} running={state.running} onClear={clearLogs} onRestart={() => send({ cmd: 'restart' })} />}
      </div>
    </main>
  )
}
