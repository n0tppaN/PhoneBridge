import { useCallback, useEffect, useMemo, useState } from 'react'
import { createHost, type Command, type HostState, type LogLine } from './bridge'

const INITIAL: HostState = {
  version: '',
  engineFound: true,
  running: false,
  status: null,
  error: null,
  settings: { camera: true, mic: true },
  driver: { installed: false, busy: false },
}

const MAX_LOG_LINES = 500

export function useBridge() {
  const host = useMemo(() => createHost(), [])
  const [state, setState] = useState<HostState>(INITIAL)
  const [logs, setLogs] = useState<LogLine[]>([])

  useEffect(() => {
    const off = host.subscribe((message) => {
      if (message.type === 'state') {
        const { type: _type, ...next } = message
        setState(next)
      } else {
        setLogs((previous) => [...previous, ...message.lines].slice(-MAX_LOG_LINES))
      }
    })
    host.send({ cmd: 'ready' }) // ask the host for the current state
    return off
  }, [host])

  const send = useCallback((command: Command) => host.send(command), [host])
  const clearLogs = useCallback(() => setLogs([]), [])

  return { state, logs, send, clearLogs, isNative: host.isNative }
}
