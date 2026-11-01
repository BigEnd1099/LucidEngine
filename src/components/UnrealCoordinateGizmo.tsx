import React from 'react';

interface UnrealCoordinateGizmoProps {
  yaw: number;
  pitch: number;
}

export const UnrealCoordinateGizmo: React.FC<UnrealCoordinateGizmoProps> = ({ yaw, pitch }) => {
  const radYaw = (yaw * Math.PI) / 180;
  const radPitch = (pitch * Math.PI) / 180;

  // Forward vector
  const fX = Math.cos(radYaw) * Math.cos(radPitch);
  const fY = Math.sin(radPitch);
  const fZ = Math.sin(radYaw) * Math.cos(radPitch);

  // Right vector (Camera X)
  const rX = -Math.sin(radYaw);
  const rY = 0;
  const rZ = Math.cos(radYaw);

  // Up vector (Camera Y = Right x Forward)
  const uX = -Math.cos(radYaw) * Math.sin(radPitch);
  const uY = Math.cos(radPitch);
  const uZ = -Math.sin(radYaw) * Math.sin(radPitch);

  const project = (vx: number, vy: number, vz: number) => {
    const sx = vx * rX + vy * rY + vz * rZ;
    const sy = vx * uX + vy * uY + vz * uZ;
    const sz = vx * fX + vy * fY + vz * fZ;
    return { sx, sy, sz };
  };

  const cx = 35;
  const cy = 35;
  const len = 22;

  const axes = [
    { name: 'X', color: '#ef4444', ...project(1, 0, 0) },
    { name: 'Y', color: '#22c55e', ...project(0, 1, 0) },
    { name: 'Z', color: '#3b82f6', ...project(0, 0, 1) },
  ].sort((a, b) => a.sz - b.sz); // Draw back-to-front

  return (
    <div className="pointer-events-none select-none flex items-center gap-1">
      <svg width="70" height="70" className="overflow-visible">
        {/* Origin dot */}
        <circle cx={cx} cy={cy} r="2" fill="#94a3b8" />

        {axes.map(axis => {
          const endX = cx + axis.sx * len;
          const endY = cy - axis.sy * len;
          const labelX = cx + axis.sx * (len + 7);
          const labelY = cy - axis.sy * (len + 7);

          return (
            <g key={axis.name}>
              {/* Axis Line */}
              <line
                x1={cx}
                y1={cy}
                x2={endX}
                y2={endY}
                stroke={axis.color}
                strokeWidth="2"
                strokeLinecap="round"
              />
              {/* Axis Tip Circle */}
              <circle cx={endX} cy={endY} r="2.5" fill={axis.color} />
              {/* Axis Label */}
              <text
                x={labelX}
                y={labelY + 3}
                fill={axis.color}
                fontSize="10"
                fontWeight="bold"
                fontFamily="sans-serif"
                textAnchor="middle"
              >
                {axis.name.toLowerCase()}
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
};
