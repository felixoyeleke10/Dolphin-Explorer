import React, { useEffect, useRef } from "react";

const NODE_COUNT = 4;
const BAR_LEVELS = [0.34, 0.52, 0.76, 0.58, 0.86];
const NODE_MOTION = [
  { top: "24%", left: "22%", dx: -0.8, dy: -0.35 },
  { top: "36%", left: "74%", dx: 0.65, dy: -0.2 },
  { top: "70%", left: "34%", dx: -0.55, dy: 0.7 },
  { top: "82%", left: "72%", dx: 0.75, dy: 0.55 },
];
const CONTOUR_CONFIGS = [
  { top: "18%", rotate: "-8deg", wave: 0.7 },
  { top: "44%", rotate: "4deg", wave: -0.5 },
  { top: "68%", rotate: "-3deg", wave: 0.4 },
];
const MOTION_PROFILES = {
  bloom:    { driftX: 10, driftY: 8,  pointerX: 9,  pointerY: 8,  tilt: 7.0, rotate: 3.6, pulse: 0.024, pulseSpeed: 0.90, waveSpeed: 0.80, sheenX: 24, sheenY: 16 },
  contour:  { driftX: 8,  driftY: 6,  pointerX: 8,  pointerY: 7,  tilt: 5.8, rotate: 2.4, pulse: 0.016, pulseSpeed: 0.75, waveSpeed: 0.70, sheenX: 20, sheenY: 13 },
  target:   { driftX: 7,  driftY: 7,  pointerX: 7,  pointerY: 7,  tilt: 5.0, rotate: 1.8, pulse: 0.014, pulseSpeed: 0.65, waveSpeed: 0.85, sheenX: 16, sheenY: 12 },
  stack:    { driftX: 6,  driftY: 6,  pointerX: 6,  pointerY: 6,  tilt: 4.5, rotate: 1.6, pulse: 0.014, pulseSpeed: 0.70, waveSpeed: 0.60, sheenX: 16, sheenY: 10 },
  orbit:    { driftX: 9,  driftY: 7,  pointerX: 7,  pointerY: 6,  tilt: 5.5, rotate: 2.8, pulse: 0.018, pulseSpeed: 0.72, waveSpeed: 0.90, sheenX: 19, sheenY: 13 },
  timeline: { driftX: 5,  driftY: 4,  pointerX: 5,  pointerY: 4,  tilt: 3.8, rotate: 1.2, pulse: 0.012, pulseSpeed: 0.80, waveSpeed: 1.20, sheenX: 14, sheenY: 10 },
  nodes:    { driftX: 8,  driftY: 6,  pointerX: 7,  pointerY: 6,  tilt: 5.2, rotate: 2.2, pulse: 0.016, pulseSpeed: 0.78, waveSpeed: 0.95, sheenX: 18, sheenY: 12 },
  panel:    { driftX: 4,  driftY: 3,  pointerX: 4,  pointerY: 4,  tilt: 3.4, rotate: 1.0, pulse: 0.012, pulseSpeed: 0.60, waveSpeed: 0.55, sheenX: 12, sheenY: 9  },
};

function joinClassNames(...names) {
  return names.filter(Boolean).join(" ");
}

function getMotionProfile(pattern) {
  return MOTION_PROFILES[pattern] || MOTION_PROFILES.bloom;
}

function hashSeed(value) {
  let hash = 0;

  for (let index = 0; index < value.length; index += 1) {
    hash = (hash * 31 + value.charCodeAt(index)) >>> 0;
  }

  return (hash % 1000) / 1000;
}

function renderPattern(pattern, label) {
  switch (pattern) {
    case "target":
      return <span className="card-visual__target" />;
    case "stack":
      return <span className="card-visual__stack" />;
    case "panel":
      return <span className="card-visual__pill">{label || "Mensor"}</span>;
    case "orbit":
      return (
        <>
          <span className="card-visual__orbit" />
          <span className="card-visual__planet" />
        </>
      );
    case "timeline":
      return (
        <span className="card-visual__bars">
          {BAR_LEVELS.map((height, index) => (
            <span
              key={index}
              className="card-visual__bar"
              style={{ "--bar-scale": height, "--bar-index": index + 1 }}
            />
          ))}
        </span>
      );
    case "nodes":
      return (
        <span className="card-visual__nodes">
          {Array.from({ length: NODE_COUNT }, (_, index) => (
            <span
              key={index}
              className="card-visual__node"
              style={{
                top: NODE_MOTION[index].top,
                left: NODE_MOTION[index].left,
                "--node-drift-x": NODE_MOTION[index].dx,
                "--node-drift-y": NODE_MOTION[index].dy,
              }}
            />
          ))}
        </span>
      );
    case "contour":
      return (
        <span className="card-visual__contours">
          {CONTOUR_CONFIGS.map((contour, index) => (
            <span
              key={contour.top}
              className="card-visual__contour"
              style={{
                "--contour-top": contour.top,
                "--contour-rotate": contour.rotate,
                "--contour-wave": contour.wave,
                "--contour-index": index + 1,
              }}
            />
          ))}
        </span>
      );
    case "bloom":
    default:
      return (
        <>
          <span className="card-visual__bloom-ring card-visual__bloom-ring--one" />
          <span className="card-visual__bloom-ring card-visual__bloom-ring--two" />
          <span className="card-visual__dot" />
        </>
      );
  }
}

export default function CardVisual({ art, className = "" }) {
  const rootRef = useRef(null);
  const normalized = typeof art === "string" ? { pattern: art } : art || {};
  const pattern = normalized.pattern || "bloom";
  const scheme = normalized.scheme || "ocean";
  const label = normalized.label || "";

  useEffect(() => {
    const element = rootRef.current;

    if (!element || typeof window === "undefined") {
      return undefined;
    }

    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");

    if (motionQuery.matches) {
      return undefined;
    }

    const profile = getMotionProfile(pattern);
    const seed = hashSeed(`${pattern}-${scheme}-${label}`);
    const tau = Math.PI * 2;
    let pointerX = 0;
    let pointerY = 0;
    let pointerActive = false;
    let rafId = 0;
    let motionX = 0;
    let motionY = 0;
    let tiltX = 0;
    let tiltY = 0;

    const handlePointerMove = (event) => {
      const bounds = element.getBoundingClientRect();
      pointerX = ((event.clientX - bounds.left) / bounds.width - 0.5) * 2;
      pointerY = ((event.clientY - bounds.top) / bounds.height - 0.5) * 2;
      pointerActive = true;
    };

    const handlePointerLeave = () => {
      pointerActive = false;
    };

    const animate = (time) => {
      const seconds = time / 1000;
      const ambientX = Math.sin(seconds * 0.62 + seed * tau) * profile.driftX;
      const ambientY = Math.cos(seconds * 0.48 + seed * tau * 1.3) * profile.driftY;
      const targetX = ambientX + (pointerActive ? pointerX * profile.pointerX : 0);
      const targetY = ambientY + (pointerActive ? pointerY * profile.pointerY : 0);
      const nextTiltX = (pointerActive ? pointerY * profile.tilt : motionY * 0.06);
      const nextTiltY = (pointerActive ? -pointerX * profile.tilt : -motionX * 0.06);
      const wave = Math.sin(seconds * profile.waveSpeed + seed * tau * 1.7);
      const pulse = 1 + Math.sin(seconds * profile.pulseSpeed + seed * tau * 0.7) * profile.pulse;
      // Sheen tracks pointer directly when active; otherwise drifts with ambient motion.
      const sheenX = pointerActive
        ? 50 + pointerX * profile.sheenX
        : 50 + Math.sin(seconds * 0.56 + seed * tau) * profile.sheenX * 0.5;
      const sheenY = pointerActive
        ? 42 + pointerY * profile.sheenY
        : 42 + Math.cos(seconds * 0.74 + seed * tau * 0.8) * profile.sheenY * 0.5;

      motionX += (targetX - motionX) * 0.08;
      motionY += (targetY - motionY) * 0.08;
      // Tilt snaps faster than motion for a more responsive feel.
      tiltX += (nextTiltX - tiltX) * 0.14;
      tiltY += (nextTiltY - tiltY) * 0.14;

      element.style.setProperty("--card-motion-x", `${motionX.toFixed(2)}px`);
      element.style.setProperty("--card-motion-y", `${motionY.toFixed(2)}px`);
      element.style.setProperty("--card-tilt-x", `${tiltX.toFixed(2)}deg`);
      element.style.setProperty("--card-tilt-y", `${tiltY.toFixed(2)}deg`);
      element.style.setProperty("--card-rotate", `${(wave * profile.rotate).toFixed(2)}deg`);
      element.style.setProperty("--card-pulse", pulse.toFixed(4));
      element.style.setProperty("--card-wave", wave.toFixed(4));
      element.style.setProperty("--card-sheen-x", `${sheenX.toFixed(2)}%`);
      element.style.setProperty("--card-sheen-y", `${sheenY.toFixed(2)}%`);

      rafId = window.requestAnimationFrame(animate);
    };

    element.addEventListener("pointermove", handlePointerMove);
    element.addEventListener("pointerleave", handlePointerLeave);

    rafId = window.requestAnimationFrame(animate);

    return () => {
      window.cancelAnimationFrame(rafId);
      element.removeEventListener("pointermove", handlePointerMove);
      element.removeEventListener("pointerleave", handlePointerLeave);
    };
  }, [label, pattern, scheme]);

  return (
    <div
      ref={rootRef}
      className={joinClassNames(
        "card-visual",
        `card-visual--pattern-${pattern}`,
        `card-visual--scheme-${scheme}`,
        className
      )}
      aria-hidden="true"
    >
      <span className="card-visual__scene">{renderPattern(pattern, label)}</span>
    </div>
  );
}
