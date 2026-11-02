// ==============================================================================
// Lucid Engine - Remote WebSocket Binary Transport Adapter (Single Port)
// Receives 60 Hz binary telemetry snapshots with uncompressed 512B backpressure
// ==============================================================================

import type { UiTransport, ConnectionState } from './UiTransport';
import {
  NET_HEADER_SIZE,
  SNAPSHOT_SIZE,
  BinaryFrameType,
  TelemetrySnapshotReader,
  type TelemetrySnapshotData
} from './TelemetrySnapshot';

export interface RemoteWsOptions {
  url?: string;
  autoReconnect?: boolean;
}

export class RemoteWsTransport implements UiTransport {
  public readonly mode = 'remote-ws' as const;

  private ws: WebSocket | null = null;
  private url: string;
  private autoReconnect: boolean;
  private isDisposed: boolean = false;
  private reconnectTimeout: any = null;
  private backoffDelay: number = 500;

  private listeners = new Set<(view: DataView) => void>();
  private connectionListeners = new Set<(state: ConnectionState) => void>();
  private connectionState: ConnectionState = 'closed';

  private lastProcessedSeq: bigint = 0n;
  private reqIdCounter: number = 1;
  private pendingCommands = new Map<number, { resolve: () => void; reject: (err: Error) => void; timer: any }>();

  // Text message handler for legacy JSON-RPC compatibility
  public onTextMessage?: (data: any) => void;

  private readonly parsedData: TelemetrySnapshotData = {
    magic: 0,
    version: 0,
    seqHead: 0n,
    cpuFrameMs: 0,
    outputLevelDb: 0,
    stateFlags: 0,
    activeEntities: 0,
    seqTail: 0n,
  };

  constructor(options?: RemoteWsOptions) {
    if (options?.url) {
      this.url = options.url;
    } else if (typeof window !== 'undefined' && window.location?.host) {
      const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      this.url = `${proto}//${window.location.host}/ws?transport=binary`;
    } else {
      this.url = 'ws://127.0.0.1:9000/ws?transport=binary';
    }
    this.autoReconnect = options?.autoReconnect ?? true;

    this.connect();
  }

  public connect(): void {
    if (this.isDisposed) return;
    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
      return;
    }

    this.setConnectionState(this.connectionState === 'closed' ? 'connecting' : 'reconnecting');

    try {
      this.ws = new WebSocket(this.url);
      this.ws.binaryType = 'arraybuffer';

      this.ws.onopen = () => {
        if (this.isDisposed) {
          this.ws?.close();
          return;
        }
        this.backoffDelay = 500;
        this.setConnectionState('connected');
      };

      this.ws.onmessage = (event: MessageEvent) => {
        if (this.isDisposed) return;

        if (event.data instanceof ArrayBuffer) {
          this.handleBinaryMessage(event.data);
        } else if (typeof event.data === 'string') {
          if (this.onTextMessage) {
            try {
              const parsed = JSON.parse(event.data);
              this.onTextMessage(parsed);
            } catch {}
          }
        }
      };

      this.ws.onclose = () => {
        if (this.isDisposed) return;
        this.cleanupPendingCommands('WebSocket closed');
        this.setConnectionState('closed');

        if (this.autoReconnect) {
          this.scheduleReconnect();
        }
      };

      this.ws.onerror = () => {
        // onclose will trigger reconnect
      };
    } catch {
      if (this.autoReconnect && !this.isDisposed) {
        this.scheduleReconnect();
      }
    }
  }

  private handleBinaryMessage(buffer: ArrayBuffer): void {
    if (buffer.byteLength < NET_HEADER_SIZE) return;

    const view = new DataView(buffer);
    const frameType = view.getUint8(0);
    const payloadLen = view.getUint32(4, true);

    if (frameType === BinaryFrameType.FULL_SNAPSHOT && payloadLen >= SNAPSHOT_SIZE) {
      if (buffer.byteLength >= NET_HEADER_SIZE + SNAPSHOT_SIZE) {
        // Zero-copy verification directly over the 8-byte aligned payload slice
        const valid = TelemetrySnapshotReader.tryReadConsistent(
          view,
          NET_HEADER_SIZE,
          this.parsedData,
          this.lastProcessedSeq
        );

        if (valid) {
          this.lastProcessedSeq = this.parsedData.seqTail;
          // Zero-allocation DataView slice for visual components
          const snapshotView = new DataView(buffer, NET_HEADER_SIZE, SNAPSHOT_SIZE);
          for (const listener of this.listeners) {
            listener(snapshotView);
          }
        }
      }
    } else if (frameType === BinaryFrameType.RPC_ACK && payloadLen >= 8) {
      const reqId = view.getUint32(NET_HEADER_SIZE + 4, true);
      const pending = this.pendingCommands.get(reqId);
      if (pending) {
        clearTimeout(pending.timer);
        this.pendingCommands.delete(reqId);
        pending.resolve();
      }
    }
  }

  public subscribeState(onSnapshot: (view: DataView) => void): () => void {
    this.listeners.add(onSnapshot);
    return () => {
      this.listeners.delete(onSnapshot);
    };
  }

  public sendCommand(commandId: number, payload?: ArrayBufferView): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.isDisposed) {
        return reject(new Error('Transport disposed'));
      }
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        return reject(new Error('WebSocket not connected'));
      }

      const reqId = this.reqIdCounter++;
      const timer = setTimeout(() => {
        if (this.pendingCommands.has(reqId)) {
          this.pendingCommands.delete(reqId);
          reject(new Error(`Binary RPC Timeout for cmd ${commandId}`));
        }
      }, 5000);

      this.pendingCommands.set(reqId, { resolve, reject, timer });

      const payloadLen = payload ? payload.byteLength : 0;
      const totalLen = NET_HEADER_SIZE + 8 + payloadLen;
      const packet = new Uint8Array(totalLen);
      const view = new DataView(packet.buffer);

      // 8-byte Network Frame Header
      view.setUint8(0, BinaryFrameType.RPC_COMMAND);
      view.setUint8(1, 0); // flags
      view.setUint16(2, 0, true); // stream_id
      view.setUint32(4, 8 + payloadLen, true);

      // Payload header: command_id (4B), request_id (4B)
      view.setUint32(NET_HEADER_SIZE, commandId, true);
      view.setUint32(NET_HEADER_SIZE + 4, reqId, true);

      if (payload && payloadLen > 0) {
        const payloadBytes = new Uint8Array(payload.buffer, payload.byteOffset, payloadLen);
        packet.set(payloadBytes, NET_HEADER_SIZE + 8);
      }

      try {
        this.ws.send(packet.buffer);
      } catch (err) {
        clearTimeout(timer);
        this.pendingCommands.delete(reqId);
        reject(err instanceof Error ? err : new Error(String(err)));
      }
    });
  }

  public sendRawJsonRpc(msg: any): void {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(msg));
    }
  }

  public onConnectionChange(listener: (state: ConnectionState) => void): () => void {
    this.connectionListeners.add(listener);
    listener(this.connectionState);
    return () => {
      this.connectionListeners.delete(listener);
    };
  }

  private setConnectionState(state: ConnectionState): void {
    if (this.connectionState !== state) {
      this.connectionState = state;
      for (const listener of this.connectionListeners) {
        listener(state);
      }
    }
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimeout) clearTimeout(this.reconnectTimeout);
    this.reconnectTimeout = setTimeout(() => {
      this.backoffDelay = Math.min(this.backoffDelay * 1.5, 5000);
      this.connect();
    }, this.backoffDelay);
  }

  private cleanupPendingCommands(reason: string): void {
    for (const [, p] of this.pendingCommands) {
      clearTimeout(p.timer);
      p.reject(new Error(reason));
    }
    this.pendingCommands.clear();
  }

  public dispose(): void {
    this.isDisposed = true;
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    this.cleanupPendingCommands('Transport disposed');
    this.listeners.clear();
    this.connectionListeners.clear();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.setConnectionState('closed');
  }
}
