// ==============================================================================
// Lucid Engine - Native IPC Shared Memory Transport Adapter
// Zero-GC continuous seqlock reader with tail -> memcpy -> head verification
// ==============================================================================

import type { UiTransport, ConnectionState } from './UiTransport';
import {
  RING_HEADER_SIZE,
  SNAPSHOT_SIZE,
  RING_SLOT_COUNT,
  NET_HEADER_SIZE,
  BinaryFrameType,
  TelemetrySnapshotReader,
  type TelemetrySnapshotData
} from './TelemetrySnapshot';

export class NativeIpcTransport implements UiTransport {
  public readonly mode = 'native-ipc' as const;

  private listeners = new Set<(view: DataView) => void>();
  private connectionListeners = new Set<(state: ConnectionState) => void>();
  private connectionState: ConnectionState = 'connecting';

  private sharedBuffer: ArrayBuffer | null = null;
  private sharedView: DataView | null = null;
  private rafId: number = 0;
  private isDisposed: boolean = false;
  private lastProcessedSeq: bigint = 0n;
  private reqIdCounter: number = 1;
  private pendingCommands = new Map<number, { resolve: () => void; reject: (err: Error) => void }>();

  // Preallocated private buffer to eliminate GC in RAF loop
  private readonly privateSnapshotBuffer = new Uint8Array(SNAPSHOT_SIZE);
  private readonly privateSnapshotView = new DataView(this.privateSnapshotBuffer.buffer);
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

  constructor(initialSharedBuffer?: ArrayBuffer) {
    if (initialSharedBuffer) {
      this.attachSharedBuffer(initialSharedBuffer);
    } else {
      this.setupNativeWebView2Bridge();
    }
  }

  private setupNativeWebView2Bridge(): void {
    const chromeApi = (window as any).chrome?.webview;
    if (chromeApi) {
      // WebView2 Shared Buffer Event
      chromeApi.addEventListener?.('sharedbufferreceived', (e: any) => {
        if (e.buffer instanceof ArrayBuffer) {
          this.attachSharedBuffer(e.buffer);
        }
      });

      // Native RPC responses
      chromeApi.addEventListener?.('message', (e: any) => {
        if (e.data instanceof ArrayBuffer && e.data.byteLength >= NET_HEADER_SIZE + 8) {
          const view = new DataView(e.data);
          const type = view.getUint8(0);
          if (type === BinaryFrameType.RPC_ACK) {
            const reqId = view.getUint32(NET_HEADER_SIZE + 4, true);
            const pending = this.pendingCommands.get(reqId);
            if (pending) {
              this.pendingCommands.delete(reqId);
              pending.resolve();
            }
          }
        }
      });

      this.setConnectionState('connected');
    } else {
      // Fallback connecting state
      this.setConnectionState('connecting');
    }
  }

  public attachSharedBuffer(buffer: ArrayBuffer): void {
    this.sharedBuffer = buffer;
    this.sharedView = new DataView(buffer);
    this.setConnectionState('connected');

    if (!this.rafId && !this.isDisposed) {
      this.startPollingLoop();
    }
  }

  private startPollingLoop(): void {
    const tick = () => {
      if (this.isDisposed) return;

      if (this.sharedView && this.listeners.size > 0) {
        // Read active_slot from header (offset 0)
        const activeSlot = this.sharedView.getUint32(0, true) % RING_SLOT_COUNT;
        const slotOffset = RING_HEADER_SIZE + activeSlot * SNAPSHOT_SIZE;

        // Perform seqlock tail -> memcpy -> head verification
        const valid = TelemetrySnapshotReader.tryReadConsistent(
          this.sharedView,
          slotOffset,
          this.parsedData,
          this.lastProcessedSeq
        );

        if (valid) {
          this.lastProcessedSeq = this.parsedData.seqTail;

          // Copy raw bytes into private buffer for listeners (zero GC)
          const sourceBytes = new Uint8Array(this.sharedBuffer!, slotOffset, SNAPSHOT_SIZE);
          this.privateSnapshotBuffer.set(sourceBytes);

          for (const listener of this.listeners) {
            listener(this.privateSnapshotView);
          }
        }
      }

      this.rafId = requestAnimationFrame(tick);
    };

    this.rafId = requestAnimationFrame(tick);
  }

  public subscribeState(onSnapshot: (view: DataView) => void): () => void {
    this.listeners.add(onSnapshot);
    if (!this.rafId && this.sharedView && !this.isDisposed) {
      this.startPollingLoop();
    }
    return () => {
      this.listeners.delete(onSnapshot);
    };
  }

  public sendCommand(commandId: number, payload?: ArrayBufferView): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.isDisposed) {
        return reject(new Error('Transport disposed'));
      }

      const reqId = this.reqIdCounter++;
      this.pendingCommands.set(reqId, { resolve, reject });

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

      const chromeApi = (window as any).chrome?.webview;
      if (chromeApi?.postMessage) {
        chromeApi.postMessage(packet.buffer);
      } else {
        // Mock immediate resolve in non-webview environments
        this.pendingCommands.delete(reqId);
        resolve();
      }
    });
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

  public dispose(): void {
    this.isDisposed = true;
    if (this.rafId) {
      cancelAnimationFrame(this.rafId);
      this.rafId = 0;
    }
    this.listeners.clear();
    this.connectionListeners.clear();
    for (const [, p] of this.pendingCommands) {
      p.reject(new Error('Transport disposed'));
    }
    this.pendingCommands.clear();
    this.setConnectionState('closed');
  }
}
