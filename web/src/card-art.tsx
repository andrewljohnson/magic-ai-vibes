// Original iconographic card art. Every illustration here is an original,
// deliberately generic vector rendering of the concept a card name suggests
// (a bolt, a lotus flower, a mill wheel, a moat around a keep...). Nothing
// reproduces or approximates any printed card illustration. Style family:
// 120x44 landscape band, 2-4 flat colors keyed to the card's color identity,
// bold silhouettes, no gradients.
import type { JSX, ReactNode } from "react";

type Art = JSX.Element;

// Palettes tuned to sit inside the existing per-color card tints.
const G = { sky: "#b9cf9c", mid: "#6f9150", dark: "#3c5730", pale: "#e2ecc9" };
const R = { sky: "#eec2a0", mid: "#c06a45", dark: "#763726", pale: "#f8e3c8" };
const U = { sky: "#b2cfdf", mid: "#54809f", dark: "#2a4c66", pale: "#e4eff5" };
const W = { sky: "#f2e8c2", mid: "#cdb87b", dark: "#6a5c38", pale: "#fdf8e2" };
const K = { sky: "#9b90a7", mid: "#584a64", dark: "#272033", pale: "#cec7d7" };
const A = { sky: "#d8d4c6", mid: "#93907f", dark: "#4d4a41", pale: "#f0ede2" };
const GOLD = "#d9a83f";
const FLAME = "#e58b3e";

function Frame({ bg, children }: { bg: string; children?: ReactNode }): Art {
  return (
    <svg
      viewBox="0 0 120 44"
      preserveAspectRatio="xMidYMid slice"
      aria-hidden="true"
      focusable="false"
    >
      <rect width="120" height="44" fill={bg} />
      {children}
    </svg>
  );
}

function Ground({ c, y = 31 }: { c: string; y?: number }) {
  return (
    <path
      d={`M0 ${y + 1} Q30 ${y - 4} 60 ${y} T120 ${y - 2} L120 44 L0 44 Z`}
      fill={c}
    />
  );
}

function Sun({
  c,
  x = 99,
  y = 9,
  r = 5,
}: {
  c: string;
  x?: number;
  y?: number;
  r?: number;
}) {
  return <circle cx={x} cy={y} r={r} fill={c} />;
}

function Pine({ x, y, s, c }: { x: number; y: number; s: number; c: string }) {
  return (
    <g fill={c}>
      <polygon
        points={`${x},${y - 3.1 * s} ${x - 0.9 * s},${y - 1.3 * s} ${
          x + 0.9 * s
        },${y - 1.3 * s}`}
      />
      <polygon
        points={`${x},${y - 2.2 * s} ${x - 1.3 * s},${y - 0.4 * s} ${
          x + 1.3 * s
        },${y - 0.4 * s}`}
      />
      <rect x={x - 0.22 * s} y={y - 0.5 * s} width={0.44 * s} height={0.6 * s} />
    </g>
  );
}

function Peak({
  x,
  w,
  h,
  c,
  cap,
  y = 33,
}: {
  x: number;
  w: number;
  h: number;
  c: string;
  cap?: string;
  y?: number;
}) {
  const top = y - h;
  return (
    <g>
      <polygon points={`${x - w},${y} ${x},${top} ${x + w},${y}`} fill={c} />
      {cap && (
        <polygon
          points={`${x - 0.28 * w},${top + 0.28 * h} ${x},${top} ${
            x + 0.28 * w
          },${top + 0.28 * h} ${x + 0.12 * w},${top + 0.2 * h} ${x},${
            top + 0.3 * h
          } ${x - 0.12 * w},${top + 0.2 * h}`}
          fill={cap}
        />
      )}
    </g>
  );
}

function waveTop(y: number, amp = 2, step = 12): string {
  let d = `M0 ${y}`;
  for (let x = 0; x < 120; x += step) {
    d += ` Q${x + step / 2} ${y - amp} ${x + step} ${y}`;
  }
  return d;
}

function Sea({ c, y, amp = 2 }: { c: string; y: number; amp?: number }) {
  return <path d={`${waveTop(y, amp)} L120 44 L0 44 Z`} fill={c} />;
}

function Bolt({
  x,
  y,
  s,
  c,
}: {
  x: number;
  y: number;
  s: number;
  c: string;
}) {
  return (
    <polygon
      points={`${x + 2 * s},${y - 12 * s} ${x - 5 * s},${y + 2 * s} ${
        x - 1 * s
      },${y + 2 * s} ${x - 2.5 * s},${y + 12 * s} ${x + 5 * s},${y - 2 * s} ${
        x + 1 * s
      },${y - 2 * s}`}
      fill={c}
    />
  );
}

function Biped({
  x,
  y,
  s,
  c,
}: {
  x: number;
  y: number;
  s: number;
  c: string;
}) {
  // Simple standing silhouette; base point (x, y), height ~3s.
  return (
    <g fill={c}>
      <circle cx={x} cy={y - 2.55 * s} r={0.45 * s} />
      <path
        d={`M${x - 0.55 * s} ${y} L${x - 0.4 * s} ${y - 1.2 * s} L${
          x - 0.55 * s
        } ${y - 2.1 * s} L${x + 0.55 * s} ${y - 2.1 * s} L${x + 0.4 * s} ${
          y - 1.2 * s
        } L${x + 0.55 * s} ${y} L${x + 0.18 * s} ${y} L${x} ${y - 0.9 * s} L${
          x - 0.18 * s
        } ${y} Z`}
      />
    </g>
  );
}

function Mox(gem: string, glow: string): Art {
  return (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={34} />
      <circle
        cx={60}
        cy={27}
        r={10}
        fill="none"
        stroke={GOLD}
        strokeWidth={5}
      />
      <polygon points="54,17 66,17 60,23" fill={GOLD} />
      <circle cx={60} cy={12.5} r={7} fill={glow} opacity={0.4} />
      <circle cx={60} cy={12.5} r={4.4} fill={gem} stroke={A.dark} />
    </Frame>
  );
}

const ART: Record<string, Art> = {
  /* ------------------------------ green ------------------------------ */
  Forest: (
    <Frame bg={G.sky}>
      <Sun c={G.pale} />
      <Ground c={G.mid} y={30} />
      <Pine x={26} y={34} s={5.4} c={G.dark} />
      <Pine x={60} y={35} s={7} c={G.dark} />
      <Pine x={92} y={33} s={5} c={G.dark} />
    </Frame>
  ),
  "Grizzly Bears": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={30} />
      <Pine x={16} y={33} s={4.5} c={G.dark} />
      <g fill="#5d4430">
        <ellipse cx={58} cy={26} rx={14} ry={8.5} />
        <circle cx={73} cy={20} r={5} />
        <circle cx={76} cy={16.5} r={1.7} />
        <polygon points="77,19 84,21 77,23" />
        <rect x={48} y={31} width={4} height={7} />
        <rect x={63} y={31} width={4} height={7} />
      </g>
    </Frame>
  ),
  "Ironroot Treefolk": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={31} />
      <g fill="#5a4a30">
        <path d="M56 38 L54 22 L50 38 Z" />
        <path d="M64 38 L66 22 L70 38 Z" />
        <rect x={54} y={12} width={12} height={16} />
        <path d="M54 16 L40 10 L42 8 L56 13 Z" />
        <path d="M66 16 L80 10 L78 8 L64 13 Z" />
      </g>
      <circle cx={60} cy={9} r={7.5} fill={G.dark} />
      <circle cx={57} cy={18} r={1.3} fill={G.pale} />
      <circle cx={63} cy={18} r={1.3} fill={G.pale} />
    </Frame>
  ),
  "Giant Growth": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={34} />
      <path d="M60 38 Q58 24 60 12" stroke={G.dark} strokeWidth={2.4} fill="none" />
      <path d="M60 14 Q44 16 44 30 Q58 28 60 14 Z" fill={G.dark} />
      <path d="M60 12 Q74 10 78 24 Q62 26 60 12 Z" fill={G.dark} />
      <g fill={G.pale}>
        <polygon points="24,18 30,8 36,18 30,15" />
        <polygon points="86,20 92,10 98,20 92,17" />
      </g>
    </Frame>
  ),
  "Llanowar Elves": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={31} />
      <Pine x={98} y={34} s={5.5} c={G.dark} />
      <g fill={G.dark}>
        <path d="M50 38 L54 18 L66 18 L70 38 Z" />
        <path d="M54 18 Q60 6 66 18 Z" />
      </g>
      <path d="M74 38 L74 12" stroke={G.dark} strokeWidth={2} />
      <path d="M74 12 Q66 8 70 2 Q78 4 74 12 Z" fill={G.mid} />
      <circle cx={60} cy={15} r={2.2} fill={G.pale} />
    </Frame>
  ),
  "Moss Beast": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={32} />
      <path
        d="M34 38 Q32 20 48 16 Q58 4 74 12 Q90 14 88 28 Q92 38 84 38 Z"
        fill={G.dark}
      />
      <circle cx={52} cy={20} r={4.5} fill={G.mid} />
      <circle cx={72} cy={17} r={3.6} fill={G.mid} />
      <circle cx={62} cy={28} r={5} fill={G.mid} />
      <circle cx={70} cy={23} r={1.4} fill={G.pale} />
      <circle cx={78} cy={23} r={1.4} fill={G.pale} />
    </Frame>
  ),
  "Forest Colossus": (
    <Frame bg={G.sky}>
      <Ground c={G.mid} y={33} />
      <Pine x={22} y={37} s={3.4} c={G.dark} />
      <Pine x={100} y={37} s={3.8} c={G.dark} />
      <g fill="#54432c">
        <path d="M50 40 L52 16 L58 16 L57 40 Z" />
        <path d="M70 40 L68 16 L62 16 L63 40 Z" />
        <rect x={50} y={10} width={20} height={10} />
        <path d="M50 14 L36 20 L35 17 L50 11 Z" />
        <path d="M70 14 L84 20 L85 17 L70 11 Z" />
      </g>
      <circle cx={60} cy={7} r={6.5} fill={G.dark} />
      <circle cx={57} cy={14} r={1.4} fill={G.pale} />
      <circle cx={63} cy={14} r={1.4} fill={G.pale} />
    </Frame>
  ),
  Channel: (
    <Frame bg={G.dark}>
      <path
        d="M60 40 Q46 34 48 22 Q50 12 60 4 Q70 12 72 22 Q74 34 60 40 Z"
        fill={G.mid}
      />
      <path
        d="M60 36 Q52 30 54 22 Q56 14 60 9 Q64 14 66 22 Q68 30 60 36 Z"
        fill={G.sky}
      />
      <path d="M60 32 Q57 26 60 17 Q63 26 60 32 Z" fill={G.pale} />
      <circle cx={30} cy={12} r={2} fill={G.sky} />
      <circle cx={92} cy={30} r={2} fill={G.sky} />
      <circle cx={88} cy={10} r={1.4} fill={G.sky} />
    </Frame>
  ),

  /* ------------------------------- red ------------------------------- */
  Mountain: (
    <Frame bg={R.sky}>
      <Sun c={R.pale} />
      <Peak x={40} w={26} h={24} c={R.dark} cap={R.pale} />
      <Peak x={78} w={30} h={19} c={R.mid} cap={R.pale} />
      <Ground c="#95502f" y={33} />
    </Frame>
  ),
  "Lightning Bolt": (
    <Frame bg={R.dark}>
      <ellipse cx={34} cy={5} rx={26} ry={7} fill="#5b2a1c" />
      <ellipse cx={86} cy={4} rx={30} ry={8} fill="#5b2a1c" />
      <Bolt x={60} y={20} s={1.5} c="#f4c95d" />
      <Ground c="#95502f" y={38} />
    </Frame>
  ),
  "Fire Elemental": (
    <Frame bg={R.mid}>
      <path
        d="M60 4 Q68 12 65 18 Q76 16 76 26 Q76 37 60 39 Q44 37 44 26 Q44 16 55 18 Q52 12 60 4 Z"
        fill={FLAME}
      />
      <path
        d="M60 12 Q65 18 63 23 Q70 23 69 29 Q68 35 60 36 Q52 35 51 29 Q50 23 57 23 Q55 18 60 12 Z"
        fill="#f4c95d"
      />
      <circle cx={56} cy={27} r={1.6} fill={R.dark} />
      <circle cx={64} cy={27} r={1.6} fill={R.dark} />
    </Frame>
  ),
  "Ironclaw Orcs": (
    <Frame bg={R.sky}>
      <Ground c={R.mid} y={31} />
      <g fill={R.dark}>
        <path d="M46 38 L48 22 L68 22 L70 38 Z" />
        <circle cx={58} cy={16} r={6} />
        <polygon points="52,14 48,8 55,11" />
        <polygon points="64,14 68,8 61,11" />
        <path d="M68 24 L80 18 L84 20 L72 27 Z" />
        <polygon points="82,19 90,12 85,20" />
        <polygon points="84,21 94,17 86,23" />
        <polygon points="85,23 93,24 85,26" />
      </g>
      <circle cx={56} cy={16} r={1.2} fill={R.pale} />
      <circle cx={61} cy={16} r={1.2} fill={R.pale} />
    </Frame>
  ),
  "Gray Ogre": (
    <Frame bg={R.sky}>
      <Ground c={R.mid} y={32} />
      <path d="M44 38 Q44 18 60 18 Q76 18 76 38 Z" fill="#7d786f" />
      <circle cx={60} cy={14} r={5.5} fill="#7d786f" />
      <rect
        x={73}
        y={6}
        width={6}
        height={20}
        rx={3}
        transform="rotate(28 76 16)"
        fill="#5c4632"
      />
      <circle cx={57} cy={13} r={1.2} fill={R.pale} />
      <circle cx={62} cy={13} r={1.2} fill={R.pale} />
    </Frame>
  ),
  "Hill Giant": (
    <Frame bg={R.sky}>
      <circle cx={60} cy={13} r={6.5} fill={R.dark} />
      <path d="M36 30 Q42 16 60 16 Q78 16 84 30 Z" fill={R.dark} />
      <circle cx={57} cy={12} r={1.3} fill={R.pale} />
      <circle cx={63} cy={12} r={1.3} fill={R.pale} />
      <Ground c={R.mid} y={29} />
      <path d="M0 44 Q26 30 56 40 Q92 32 120 42 L120 44 Z" fill="#95502f" />
    </Frame>
  ),
  Disintegrate: (
    <Frame bg={R.dark}>
      <polygon points="0,6 76,18 76,26 0,16" fill={FLAME} />
      <polygon points="0,9 74,20 74,24 0,13" fill="#f4c95d" />
      <g fill={R.sky}>
        <path d="M78 12 Q86 14 86 22 Q86 32 78 34 L76 12 Z" />
        <rect x={90} y={12} width={4} height={4} />
        <rect x={97} y={18} width={3} height={3} />
        <rect x={92} y={26} width={3.4} height={3.4} />
        <rect x={103} y={10} width={2.2} height={2.2} />
        <rect x={104} y={24} width={2} height={2} />
        <rect x={110} y={17} width={1.8} height={1.8} />
      </g>
    </Frame>
  ),
  "Wheel of Fortune": (
    <Frame bg={R.mid}>
      <circle cx={60} cy={22} r={16} fill={R.dark} />
      <circle cx={60} cy={22} r={12.5} fill={R.sky} />
      <g stroke={R.dark} strokeWidth={2.6}>
        <path d="M60 10 L60 34" />
        <path d="M48 22 L72 22" />
        <path d="M51.5 13.5 L68.5 30.5" />
        <path d="M68.5 13.5 L51.5 30.5" />
      </g>
      <circle cx={60} cy={22} r={3.6} fill={GOLD} />
      <polygon points="60,1 63,6 57,6" fill={GOLD} />
    </Frame>
  ),
  Shatter: (
    <Frame bg={R.mid}>
      <g fill={A.mid}>
        <circle cx={54} cy={22} r={12} />
        <rect x={51} y={6} width={6} height={5} />
        <rect x={51} y={33} width={6} height={5} />
        <rect x={38} y={19} width={5} height={6} />
        <rect x={65} y={19} width={5} height={6} />
        <rect x={42} y={9} width={6} height={6} transform="rotate(45 45 12)" />
        <rect x={42} y={29} width={6} height={6} transform="rotate(45 45 32)" />
      </g>
      <circle cx={54} cy={22} r={5} fill={R.mid} />
      <polygon points="60,2 66,16 58,20 68,30 62,42 70,26 62,21 70,8" fill={R.sky} />
      <g fill={A.pale}>
        <rect x={84} y={10} width={4.5} height={4.5} transform="rotate(20 86 12)" />
        <rect x={92} y={22} width={3.6} height={3.6} transform="rotate(40 94 24)" />
        <rect x={86} y={32} width={3} height={3} transform="rotate(65 87 33)" />
      </g>
    </Frame>
  ),

  /* ------------------------------- blue ------------------------------ */
  Island: (
    <Frame bg={U.sky}>
      <Sun c={U.pale} x={22} />
      <Sea c={U.mid} y={27} amp={2.4} />
      <path d="M44 28 Q60 15 76 28 Z" fill={U.dark} />
      <path d="M60 20 Q57 14 60 8" stroke={U.dark} strokeWidth={1.8} fill="none" />
      <path
        d="M60 8 Q52 6 48 10 M60 8 Q60 2 55 1 M60 8 Q66 2 70 5 M60 8 Q68 8 71 12"
        stroke={G.dark}
        strokeWidth={1.8}
        fill="none"
      />
      <Sea c={U.dark} y={38} amp={1.6} />
    </Frame>
  ),
  Counterspell: (
    <Frame bg={U.mid}>
      <circle cx={60} cy={22} r={15} fill={U.sky} />
      <g fill={GOLD}>
        <polygon points="60,10 63,19 72,22 63,25 60,34 57,25 48,22 57,19" />
      </g>
      <circle
        cx={60}
        cy={22}
        r={17}
        fill="none"
        stroke={U.dark}
        strokeWidth={4.4}
      />
      <rect
        x={41}
        y={19.4}
        width={38}
        height={5.2}
        transform="rotate(-45 60 22)"
        fill={U.dark}
      />
    </Frame>
  ),
  "Water Elemental": (
    <Frame bg={U.sky}>
      <Sea c={U.mid} y={30} amp={2.4} />
      <path
        d="M40 32 Q40 12 58 10 Q76 8 78 20 Q79 28 70 26 Q76 32 66 33 Q54 35 40 32 Z"
        fill={U.dark}
      />
      <path d="M58 10 Q66 8 72 13 Q64 14 58 10 Z" fill={U.pale} />
      <circle cx={64} cy={18} r={1.8} fill={U.pale} />
      <path d="M44 30 Q52 27 60 30" stroke={U.pale} strokeWidth={1.6} fill="none" />
    </Frame>
  ),
  Tsunami: (
    <Frame bg={U.sky}>
      <path
        d="M0 44 L0 18 Q2 4 22 4 Q46 4 52 16 Q56 24 46 24 Q54 30 44 32 Q60 34 78 38 L120 44 Z"
        fill={U.dark}
      />
      <path
        d="M2 18 Q6 8 22 8 Q40 8 46 16 Q40 12 28 12 Q12 12 2 18 Z"
        fill={U.pale}
      />
      <path d="M96 36 Q99 30 96 25" stroke={G.dark} strokeWidth={1.6} fill="none" />
      <path
        d="M96 25 Q90 22 88 25 M96 25 Q96 19 92 18 M96 25 Q101 19 105 21"
        stroke={G.dark}
        strokeWidth={1.6}
        fill="none"
      />
      <path d="M86 38 Q96 34 108 38 L108 40 L86 40 Z" fill={W.mid} />
    </Frame>
  ),
  "Flying Men": (
    <Frame bg={U.sky}>
      <ellipse cx={24} cy={34} rx={16} ry={4} fill={U.pale} />
      <ellipse cx={98} cy={10} rx={17} ry={4.5} fill={U.pale} />
      <g fill={U.dark}>
        <polygon points="46,14 30,6 42,17" />
        <polygon points="50,14 66,6 54,17" />
        <Biped x={48} y={26} s={4} c={U.dark} />
        <polygon points="76,30 64,24 73,32" />
        <polygon points="79,30 91,24 82,32" />
      </g>
      <Biped x={77.5} y={39} s={3} c={U.dark} />
    </Frame>
  ),
  "Air Elemental": (
    <Frame bg={U.sky}>
      <g fill={U.pale}>
        <circle cx={46} cy={22} r={11} />
        <circle cx={62} cy={16} r={12} />
        <circle cx={76} cy={24} r={9} />
        <path d="M35 30 Q60 38 86 29 L86 33 Q60 41 35 34 Z" />
      </g>
      <path
        d="M84 22 Q96 18 104 24 M88 28 Q98 26 108 31"
        stroke={U.mid}
        strokeWidth={2}
        fill="none"
      />
      <circle cx={58} cy={19} r={1.8} fill={U.dark} />
      <circle cx={67} cy={19} r={1.8} fill={U.dark} />
    </Frame>
  ),
  "Ancestral Recall": (
    <Frame bg={U.mid}>
      <g stroke={U.pale} strokeWidth={1.6} fill="none">
        <path d="M32 8 Q60 -2 88 8" />
        <path d="M36 13 Q60 4 84 13" />
      </g>
      <g fill={U.pale} stroke={U.dark}>
        <rect x={36} y={16} width={15} height={21} rx={2} transform="rotate(-14 43 26)" />
        <rect x={52.5} y={14} width={15} height={22} rx={2} />
        <rect x={69} y={16} width={15} height={21} rx={2} transform="rotate(14 77 26)" />
      </g>
      <g fill={U.dark}>
        <circle cx={44} cy={26} r={2.4} />
        <circle cx={60} cy={25} r={2.4} />
        <circle cx={76} cy={26} r={2.4} />
      </g>
    </Frame>
  ),
  "Time Walk": (
    <Frame bg={U.dark}>
      <ellipse
        cx={60}
        cy={22}
        rx={34}
        ry={12}
        fill="none"
        stroke={U.mid}
        strokeWidth={2}
      />
      <g fill={GOLD}>
        <path d="M50 8 L70 8 L62 20 L70 36 L50 36 L58 20 Z" />
      </g>
      <path d="M54 12 L66 12 L60 19 Z" fill={U.dark} />
      <path d="M56 33 L64 33 L60 26 Z" fill={U.dark} />
      <circle cx={26} cy={20} r={2.4} fill={U.pale} />
      <circle cx={94} cy={24} r={2.4} fill={U.pale} />
    </Frame>
  ),
  Braingeyser: (
    <Frame bg={U.sky}>
      <Sea c={U.mid} y={35} amp={2} />
      <path d="M54 40 Q52 20 46 12 Q60 20 60 40 Z" fill={U.dark} />
      <path d="M66 40 Q68 20 74 12 Q60 20 60 40 Z" fill={U.dark} />
      <path d="M56 40 Q60 14 64 40 Z" fill={U.pale} />
      <g fill={U.dark}>
        <circle cx={42} cy={8} r={2.2} />
        <circle cx={60} cy={4} r={2.6} />
        <circle cx={78} cy={8} r={2.2} />
        <circle cx={50} cy={14} r={1.6} />
        <circle cx={70} cy={14} r={1.6} />
      </g>
    </Frame>
  ),
  "Force Spike": (
    <Frame bg={U.mid}>
      <polygon points="60,2 66,34 54,34" fill={U.pale} />
      <polygon points="60,10 63,32 57,32" fill={U.dark} />
      <g stroke={U.pale} strokeWidth={1.8} fill="none">
        <path d="M44 12 L52 18" />
        <path d="M76 12 L68 18" />
        <path d="M40 26 L50 28" />
        <path d="M80 26 L70 28" />
      </g>
      <Sea c={U.dark} y={37} amp={1.6} />
    </Frame>
  ),
  "Serendib Efreet": (
    <Frame bg={U.mid}>
      <g fill={U.dark}>
        <polygon points="44,26 18,10 40,32" />
        <polygon points="76,26 102,10 80,32" />
        <path d="M48 40 Q48 22 60 22 Q72 22 72 40 Q60 34 48 40 Z" />
        <circle cx={60} cy={16} r={6.5} />
        <polygon points="55,12 50,3 58,9" />
        <polygon points="65,12 70,3 62,9" />
      </g>
      <circle cx={57} cy={16} r={1.4} fill={GOLD} />
      <circle cx={63} cy={16} r={1.4} fill={GOLD} />
    </Frame>
  ),
  "Psionic Blast": (
    <Frame bg={U.dark}>
      <g fill={U.mid}>
        <circle cx={34} cy={18} r={9} />
        <path d="M26 40 Q26 24 34 24 Q42 24 42 40 Z" />
      </g>
      <g stroke={U.pale} strokeWidth={2.4} fill="none">
        <path d="M48 10 Q56 22 48 34" />
        <path d="M58 6 Q69 22 58 38" />
        <path d="M68 2 Q82 22 68 42" />
      </g>
      <polygon
        points="98,12 102,18 110,16 104,22 108,30 100,25 94,32 96,23 88,20 96,19"
        fill={GOLD}
      />
    </Frame>
  ),
  "Sage of Lat-Nam": (
    <Frame bg={U.sky}>
      <Ground c={U.mid} y={33} />
      <g fill={U.dark}>
        <path d="M44 40 L48 16 L68 16 L72 40 Z" />
        <path d="M48 16 Q58 4 68 16 Z" />
      </g>
      <path d="M50 26 Q60 21 70 26 L70 34 Q60 29 50 34 Z" fill={U.pale} />
      <g stroke={U.mid} strokeWidth={1.2} fill="none">
        <path d="M53 28 Q60 24.5 67 28" />
        <path d="M53 31 Q60 27.5 67 31" />
      </g>
    </Frame>
  ),
  "Mana Drain": (
    <Frame bg={U.dark}>
      <g stroke={U.pale} strokeWidth={2.2} fill="none">
        <path d="M20 6 Q60 18 100 6" />
        <path d="M32 12 Q60 24 88 12" />
        <path d="M44 18 Q60 28 76 18" />
        <path d="M54 24 Q60 30 66 24" />
      </g>
      <path d="M48 30 L72 30 L66 38 L54 38 Z" fill={GOLD} />
      <circle cx={60} cy={30} r={4} fill={U.mid} />
    </Frame>
  ),
  Recall: (
    <Frame bg={U.mid}>
      <Ground c={U.dark} y={36} />
      <g fill={U.pale} stroke={U.dark}>
        <rect x={40} y={16} width={13} height={19} rx={2} transform="rotate(-8 46 25)" />
        <rect x={62} y={15} width={13} height={19} rx={2} transform="rotate(8 69 25)" />
      </g>
      <path
        d="M84 30 Q104 24 96 10 Q88 0 60 4"
        stroke={GOLD}
        strokeWidth={2.6}
        fill="none"
      />
      <polygon points="60,4 70,0 68,9" fill={GOLD} />
    </Frame>
  ),
  Timetwister: (
    <Frame bg={U.dark}>
      <g stroke={U.mid} strokeWidth={2.4} fill="none">
        <path d="M60 22 Q60 6 76 8 Q94 10 92 26 Q90 40 70 40" />
        <path d="M60 22 Q60 38 44 36 Q26 34 28 18 Q30 4 50 4" />
      </g>
      <g fill={GOLD}>
        <path d="M52 12 L68 12 L61.5 22 L68 33 L52 33 L58.5 22 Z" />
      </g>
      <path d="M55 15 L65 15 L60 21 Z" fill={U.dark} />
      <path d="M56.5 30.5 L63.5 30.5 L60 25 Z" fill={U.dark} />
    </Frame>
  ),
  "Copy Artifact": (
    <Frame bg={U.sky}>
      <Ground c={U.mid} y={35} />
      <g fill={A.mid} stroke={A.dark}>
        <polygon points="44,8 58,14 58,30 44,36 30,30 30,14" />
      </g>
      <polygon points="44,14 52,17.5 52,26.5 44,30 36,26.5 36,17.5" fill={A.pale} />
      <g fill="none" stroke={U.dark} strokeWidth={2.2} strokeDasharray="4 3">
        <polygon points="78,8 92,14 92,30 78,36 64,30 64,14" />
      </g>
      <polygon
        points="78,14 86,17.5 86,26.5 78,30 70,26.5 70,17.5"
        fill="none"
        stroke={U.dark}
        strokeWidth={1.6}
        strokeDasharray="3 2.5"
      />
    </Frame>
  ),

  /* ------------------------------ white ------------------------------ */
  Plains: (
    <Frame bg={W.sky}>
      <Sun c={GOLD} />
      <Ground c={W.mid} y={26} />
      <g stroke={W.pale} strokeWidth={1.8} fill="none">
        <path d="M0 32 Q40 27 78 31 T120 29" />
        <path d="M0 38 Q44 32 84 37 T120 35" />
      </g>
    </Frame>
  ),
  "Savannah Lions": (
    <Frame bg={W.sky}>
      <Sun c={GOLD} x={16} y={10} r={6} />
      <Ground c={W.mid} y={33} />
      <g fill={W.dark}>
        <path d="M46 24 Q60 14 76 20 L88 26 L84 31 L72 26 Q58 22 50 29 Z" />
        <circle cx={45} cy={20} r={8} />
        <circle cx={42} cy={17} r={4.5} fill="#9c8347" />
        <polygon points="34,28 40,22 44,27 38,33" />
        <polygon points="70,26 66,36 71,36 75,27" />
        <polygon points="82,28 88,36 92,35 87,26" />
        <path d="M88 24 Q98 20 100 12" stroke={W.dark} strokeWidth={2.4} fill="none" />
      </g>
    </Frame>
  ),
  "Serra Angel": (
    <Frame bg={W.sky}>
      <g fill={W.pale} stroke={W.mid}>
        <path d="M52 30 Q28 26 20 8 Q40 12 54 22 Z" />
        <path d="M68 30 Q92 26 100 8 Q80 12 66 22 Z" />
      </g>
      <path d="M52 42 L55 16 L65 16 L68 42 Z" fill={GOLD} />
      <circle cx={60} cy={11} r={5.5} fill={W.dark} />
      <circle cx={60} cy={5} r={7} fill="none" stroke={GOLD} strokeWidth={1.8} />
      <path d="M72 40 L74 18" stroke={W.dark} strokeWidth={2} />
      <path d="M70 22 L78 22" stroke={W.dark} strokeWidth={2} />
    </Frame>
  ),
  "Benalish Hero": (
    <Frame bg={W.sky}>
      <Ground c={W.mid} y={32} />
      <Biped x={58} y={38} s={9} c={W.dark} />
      <path d="M44 16 L54 16 L54 28 L49 33 L44 28 Z" fill={GOLD} stroke={W.dark} />
      <path d="M49 16 L49 33 M44 23 L54 23" stroke={W.dark} strokeWidth={1.4} />
      <path d="M72 38 L72 6" stroke={W.dark} strokeWidth={2} />
      <polygon points="72,6 88,9 72,14" fill={GOLD} />
    </Frame>
  ),
  "Mesa Pegasus": (
    <Frame bg={W.sky}>
      <path d="M8 34 L20 20 L48 20 L58 34 Z" fill={W.mid} />
      <Ground c={W.mid} y={38} />
      <g fill={W.pale} stroke={W.dark} strokeWidth={1.2}>
        <path d="M60 28 Q60 12 44 6 Q62 8 70 16 Z" />
        <path d="M62 20 Q78 14 88 18 Q96 21 94 14 L100 18 Q100 26 90 26 L86 34 L82 34 L84 27 L70 28 L68 35 L64 35 L64 26 Z" />
      </g>
      <path d="M94 14 Q98 10 96 6" stroke={W.dark} strokeWidth={1.6} fill="none" />
      <circle cx={94} cy={17} r={1.1} fill={W.dark} />
    </Frame>
  ),
  "Thunder Spirit": (
    <Frame bg={W.mid}>
      <g fill={W.pale} stroke={W.dark} strokeWidth={1}>
        <path d="M52 26 Q30 24 24 8 Q42 12 54 20 Z" />
        <path d="M68 26 Q90 24 96 8 Q78 12 66 20 Z" />
      </g>
      <path d="M54 38 L56 14 L64 14 L66 38 Z" fill={W.dark} />
      <circle cx={60} cy={10} r={5} fill={W.dark} />
      <Bolt x={82} y={30} s={0.9} c={GOLD} />
    </Frame>
  ),
  "White Knight": (
    <Frame bg={W.sky}>
      <path
        d="M46 40 L46 16 Q46 6 60 6 Q74 6 74 16 L74 40 Z"
        fill={W.pale}
        stroke={W.dark}
        strokeWidth={1.6}
      />
      <rect x={46} y={22} width={28} height={4} fill={W.dark} />
      <path d="M58 6 Q52 0 44 2 Q46 8 52 9 Z" fill={GOLD} />
      <path d="M50 28 L70 28 M60 26 L60 38" stroke={W.mid} strokeWidth={1.6} />
      <polygon points="88,12 92,20 100,22 92,24 88,32 84,24 76,22 84,20" fill={GOLD} />
    </Frame>
  ),
  Crusade: (
    <Frame bg={W.sky}>
      <Ground c={W.mid} y={35} />
      <path d="M40 40 L40 4" stroke={W.dark} strokeWidth={2.4} />
      <path d="M40 6 L92 6 L80 15 L92 24 L40 24 Z" fill={GOLD} />
      <polygon
        points="62,8 64.5,13 70,13.5 66,17 67.5,22 62,19.5 56.5,22 58,17 54,13.5 59.5,13"
        fill={W.pale}
      />
      <path d="M28 40 L28 16 M22 20 L34 20" stroke={W.dark} strokeWidth={2} />
    </Frame>
  ),
  Disenchant: (
    <Frame bg={W.sky}>
      <g stroke={GOLD} strokeWidth={2} fill="none">
        <path d="M60 2 L60 10" />
        <path d="M60 34 L60 42" />
        <path d="M40 22 L32 22" />
        <path d="M80 22 L88 22" />
        <path d="M46 8 L41 3" />
        <path d="M74 8 L79 3" />
        <path d="M46 36 L41 41" />
        <path d="M74 36 L79 41" />
      </g>
      <path
        d="M58 8 A14 14 0 0 0 52 34"
        fill="none"
        stroke={K.mid}
        strokeWidth={5}
      />
      <path
        d="M66 9 A14 14 0 0 1 62 35"
        fill="none"
        stroke={K.mid}
        strokeWidth={5}
      />
      <polygon points="60,12 63,20 60,26 57,18" fill={W.pale} stroke={W.dark} />
    </Frame>
  ),
  "Swords to Plowshares": (
    <Frame bg={W.sky}>
      <Ground c={W.mid} y={34} />
      <g fill={A.mid} stroke={W.dark} strokeWidth={1}>
        <polygon points="30,4 34,4 34,26 32,32 30,26" />
        <rect x={24} y={12} width={16} height={3.4} rx={1.4} />
      </g>
      <path d="M52 20 L68 20 M62 14 L68 20 L62 26" stroke={W.dark} strokeWidth={2.4} fill="none" />
      <path
        d="M78 8 L82 8 L84 24 Q94 24 98 32 L80 32 Q76 20 78 8 Z"
        fill={A.mid}
        stroke={W.dark}
        strokeWidth={1}
      />
      <path d="M84 34 Q92 30 100 34" stroke={W.pale} strokeWidth={1.8} fill="none" />
    </Frame>
  ),
  Armageddon: (
    <Frame bg={R.dark}>
      <g fill={GOLD}>
        <polygon points="60,0 50,26 58,26" />
        <polygon points="60,0 70,26 62,26" />
        <polygon points="24,0 40,24 48,22" />
        <polygon points="96,0 80,24 72,22" />
      </g>
      <Ground c={K.dark} y={30} />
      <g stroke={GOLD} strokeWidth={1.8} fill="none">
        <path d="M30 44 L36 34 L32 30" />
        <path d="M60 44 L58 33 L64 28" />
        <path d="M90 44 L86 35 L92 31" />
      </g>
    </Frame>
  ),
  Moat: (
    <Frame bg={W.sky}>
      <Ground c={G.mid} y={30} />
      <ellipse cx={60} cy={33} rx={40} ry={9} fill={U.mid} />
      <ellipse cx={60} cy={33} rx={26} ry={5.5} fill={G.mid} />
      <g fill={W.pale} stroke={W.dark} strokeWidth={1}>
        <rect x={52} y={12} width={16} height={22} />
        <rect x={50} y={8} width={4} height={5} />
        <rect x={58} y={8} width={4} height={5} />
        <rect x={66} y={8} width={4} height={5} />
      </g>
      <rect x={57.5} y={24} width={5} height={10} fill={W.dark} />
      <path d="M60 8 L60 2 L68 4 L60 6" fill={GOLD} stroke={GOLD} />
    </Frame>
  ),

  /* ------------------------------ black ------------------------------ */
  Swamp: (
    <Frame bg={K.sky}>
      <Sun c={K.pale} x={20} y={8} r={4.5} />
      <Sea c={K.dark} y={31} amp={1.6} />
      <g stroke={K.dark} strokeWidth={2.2} fill="none">
        <path d="M78 32 L78 12" />
        <path d="M78 16 Q70 12 66 6" />
        <path d="M78 14 Q86 10 92 12" />
        <path d="M78 20 Q86 18 90 20" />
      </g>
      <g stroke={K.mid} strokeWidth={1.4} fill="none">
        <path d="M30 32 L30 24" />
        <path d="M34 32 L34 22" />
        <path d="M38 32 L38 25" />
      </g>
    </Frame>
  ),
  "Juzam Djinn": (
    <Frame bg={K.sky}>
      <path d="M60 44 Q50 40 52 30" stroke={K.mid} strokeWidth={5} fill="none" />
      <g fill={K.dark}>
        <path d="M34 42 Q36 22 60 22 Q84 22 86 42 Z" />
        <circle cx={60} cy={15} r={8} />
        <path d="M53 11 Q44 10 42 2 Q52 2 55 8 Z" />
        <path d="M67 11 Q76 10 78 2 Q68 2 65 8 Z" />
      </g>
      <circle cx={56.5} cy={15} r={1.6} fill={GOLD} />
      <circle cx={63.5} cy={15} r={1.6} fill={GOLD} />
      <path d="M40 34 L46 34" stroke={GOLD} strokeWidth={2.4} />
      <path d="M74 34 L80 34" stroke={GOLD} strokeWidth={2.4} />
    </Frame>
  ),
  "Sedge Troll": (
    <Frame bg={K.sky}>
      <g fill="#4a5c3a">
        <path d="M38 34 Q40 22 52 20 L50 12 Q54 8 58 12 Q60 6 66 8 Q64 12 64 16 Q76 18 80 26 L84 34 Z" />
        <circle cx={58} cy={16} r={7} />
        <polygon points="52,13 48,7 55,10" />
        <polygon points="64,13 68,7 61,10" />
      </g>
      <circle cx={55.5} cy={16} r={1.4} fill={GOLD} />
      <circle cx={61.5} cy={16} r={1.4} fill={GOLD} />
      <Sea c={K.dark} y={33} amp={1.8} />
      <g stroke={K.mid} strokeWidth={1.4} fill="none">
        <path d="M22 33 L22 24" />
        <path d="M26 33 L26 26" />
        <path d="M96 33 L96 25" />
        <path d="M100 33 L100 27" />
      </g>
    </Frame>
  ),
  "Hypnotic Specter": (
    <Frame bg={K.sky}>
      <g fill={K.dark}>
        <path d="M60 4 Q74 8 74 22 L76 34 L70 30 L68 38 L62 32 L58 40 L52 32 L48 37 L46 26 Q44 8 60 4 Z" />
      </g>
      <path
        d="M60 16 Q56 16 56 19 Q56 22 60 22 Q63 22 63 19.5 Q63 17.5 60.5 17.5 Q59 17.5 59 19"
        stroke={GOLD}
        strokeWidth={1.6}
        fill="none"
      />
      <path d="M28 30 Q40 24 46 26 M92 28 Q82 24 76 26" stroke={K.mid} strokeWidth={1.8} fill="none" />
    </Frame>
  ),
  "Dark Ritual": (
    <Frame bg={K.dark}>
      <rect x={34} y={32} width={52} height={7} fill={K.mid} />
      <g fill={K.pale}>
        <path d="M44 30 Q38 22 44 14 Q50 22 44 30 Z" />
        <path d="M60 30 Q52 18 60 6 Q68 18 60 30 Z" />
        <path d="M76 30 Q70 22 76 14 Q82 22 76 30 Z" />
      </g>
      <g fill={K.dark}>
        <path d="M60 28 Q56 21 60 13 Q64 21 60 28 Z" />
      </g>
      <g fill={K.mid}>
        <path d="M44 28 Q41 23 44 18 Q47 23 44 28 Z" />
        <path d="M76 28 Q73 23 76 18 Q79 23 76 28 Z" />
      </g>
    </Frame>
  ),
  "Demonic Tutor": (
    <Frame bg={K.dark}>
      <g fill={K.pale} stroke={K.mid}>
        <path d="M60 20 Q46 14 34 18 L34 36 Q46 32 60 38 Z" />
        <path d="M60 20 Q74 14 86 18 L86 36 Q74 32 60 38 Z" />
      </g>
      <g stroke={K.mid} strokeWidth={1.2} fill="none">
        <path d="M40 22 Q50 20 56 24 M40 27 Q50 25 56 29" />
        <path d="M80 22 Q70 20 64 24 M80 27 Q70 25 64 29" />
      </g>
      <path d="M46 8 Q60 -2 74 8 Q60 16 46 8 Z" fill={K.mid} />
      <circle cx={60} cy={7} r={3.2} fill={GOLD} />
      <circle cx={60} cy={7} r={1.3} fill={K.dark} />
    </Frame>
  ),
  "Mind Twist": (
    <Frame bg={K.sky}>
      <g fill={K.dark}>
        <path d="M42 42 Q34 28 40 16 Q46 4 60 4 Q74 4 78 16 L74 20 L78 26 L72 28 L74 34 Q60 30 58 42 Z" />
      </g>
      <path
        d="M56 22 Q66 12 72 20 Q76 26 68 28 Q62 29 62 24 Q62 20 66 21"
        stroke={K.pale}
        strokeWidth={2}
        fill="none"
      />
      <g fill={K.pale}>
        <rect x={86} y={10} width={4} height={4} transform="rotate(24 88 12)" />
        <rect x={94} y={20} width={3.2} height={3.2} transform="rotate(48 96 22)" />
        <rect x={88} y={30} width={2.6} height={2.6} transform="rotate(70 89 31)" />
      </g>
    </Frame>
  ),

  /* --------------------------- special lands -------------------------- */
  Plateau: (
    <Frame bg={W.sky}>
      <Sun c={GOLD} x={20} y={9} />
      <polygon points="40,33 48,12 88,12 96,33" fill={R.mid} />
      <rect x={48} y={12} width={40} height={4} fill={R.dark} />
      <Ground c={W.mid} y={33} />
    </Frame>
  ),
  Tundra: (
    <Frame bg={U.sky}>
      <Sun c={W.pale} x={22} y={9} />
      <Peak x={52} w={22} h={16} c={W.pale} y={30} />
      <Peak x={82} w={18} h={12} c={W.pale} y={30} />
      <Peak x={60} w={9} h={7} c={U.mid} y={30} />
      <Ground c={U.pale} y={30} />
      <path d="M14 38 Q28 35 42 38 M70 40 Q86 37 102 40" stroke={U.mid} strokeWidth={1.6} fill="none" />
    </Frame>
  ),
  "Volcanic Island": (
    <Frame bg={U.sky}>
      <ellipse cx={62} cy={9} rx={12} ry={4.5} fill={K.sky} />
      <polygon points="34,32 54,8 70,8 90,32" fill={R.dark} />
      <polygon points="54,8 70,8 66,15 62,11 58,16" fill={FLAME} />
      <path d="M62 12 Q60 20 64 28" stroke={FLAME} strokeWidth={2.6} fill="none" />
      <Sea c={U.mid} y={31} amp={2.2} />
    </Frame>
  ),
  "Underground Sea": (
    <Frame bg={K.dark}>
      <Sea c={U.mid} y={28} amp={2} />
      <path d="M0 0 L120 0 L120 10 Q104 4 92 14 L84 6 Q70 16 58 8 Q44 18 30 8 Q18 16 0 10 Z" fill={K.dark} />
      <g fill={K.dark}>
        <polygon points="24,8 30,24 36,8" />
        <polygon points="78,8 84,20 90,8" />
      </g>
      <path d="M20 34 Q34 31 46 34 M66 38 Q82 35 98 38" stroke={U.pale} strokeWidth={1.6} fill="none" />
    </Frame>
  ),
  Badlands: (
    <Frame bg={R.sky}>
      <Sun c={R.pale} x={18} y={9} />
      <g fill={K.dark}>
        <polygon points="34,36 37,8 45,8 48,36" />
        <rect x={33} y={7} width={16} height={4} rx={2} />
        <polygon points="62,36 64,14 72,14 74,36" />
        <rect x={61} y={13} width={14} height={3.6} rx={1.8} />
        <polygon points="88,36 90,18 96,18 98,36" />
        <rect x={87} y={17} width={12} height={3.4} rx={1.7} />
      </g>
      <Ground c="#6e3a2c" y={35} />
    </Frame>
  ),
  "City of Brass": (
    <Frame bg={R.sky}>
      <Sun c={R.pale} x={16} y={8} />
      <g fill={GOLD}>
        <rect x={30} y={20} width={4} height={16} />
        <circle cx={32} cy={18} r={3} />
        <rect x={42} y={14} width={16} height={22} />
        <path d="M42 14 Q50 4 58 14 Z" />
        <rect x={64} y={18} width={13} height={18} />
        <path d="M64 18 Q70.5 10 77 18 Z" />
        <rect x={84} y={22} width={4} height={14} />
        <circle cx={86} cy={19} r={3} />
      </g>
      <g fill={R.dark}>
        <rect x={47} y={24} width={6} height={12} />
        <rect x={68} y={26} width={5} height={10} />
      </g>
      <Ground c="#95502f" y={37} />
    </Frame>
  ),
  "Library of Alexandria": (
    <Frame bg={W.sky}>
      <Sun c={GOLD} x={16} y={8} r={4.5} />
      <polygon points="36,14 60,4 84,14" fill={W.dark} />
      <g fill={W.pale} stroke={W.dark} strokeWidth={1}>
        <rect x={40} y={16} width={5} height={16} />
        <rect x={51} y={16} width={5} height={16} />
        <rect x={62} y={16} width={5} height={16} />
        <rect x={73} y={16} width={5} height={16} />
      </g>
      <rect x={34} y={32} width={52} height={3.4} fill={W.mid} />
      <rect x={30} y={36} width={60} height={3.4} fill={W.mid} />
    </Frame>
  ),
  "Mishra's Factory": (
    <Frame bg={A.sky}>
      <g fill={A.pale}>
        <circle cx={22} cy={9} r={4} />
        <circle cx={30} cy={6} r={5} />
        <circle cx={39} cy={5} r={4} />
      </g>
      <rect x={18} y={12} width={7} height={22} fill={A.dark} />
      <path d="M34 34 L34 22 L50 14 L50 22 L66 14 L66 22 L82 14 L82 34 Z" fill={A.mid} />
      <rect x={34} y={30} width={48} height={6} fill={A.dark} />
      <g fill={A.dark}>
        <circle cx={94} cy={26} r={9} />
        <rect x={92.4} y={14.5} width={3.2} height={5} />
        <rect x={92.4} y={32.5} width={3.2} height={5} />
        <rect x={82.5} y={24.4} width={5} height={3.2} />
        <rect x={100.5} y={24.4} width={5} height={3.2} />
      </g>
      <circle cx={94} cy={26} r={4} fill={A.sky} />
      <Ground c={A.mid} y={38} />
    </Frame>
  ),
  "Strip Mine": (
    <Frame bg={R.sky}>
      <g fill="#8a5a3b">
        <path d="M0 16 L120 16 L104 22 L88 22 L88 28 L74 28 L74 34 L60 34 L60 40 L34 40 L34 34 L24 34 L24 28 L12 28 L12 22 L0 22 Z" />
      </g>
      <g stroke={A.dark} strokeWidth={2.2} fill="none">
        <path d="M92 6 L102 14" />
        <path d="M90 13 Q97 4 106 8" />
      </g>
      <Ground c="#6e3a2c" y={41} />
    </Frame>
  ),

  /* ----------------------------- artifacts ---------------------------- */
  "Mox Pearl": Mox("#f3ead3", "#fffdf2"),
  "Mox Sapphire": Mox("#3f6fb3", "#9dc2ea"),
  "Mox Ruby": Mox("#b33a3a", "#e89a8a"),
  "Mox Jet": Mox("#241f2b", "#8d829e"),
  "Mox Emerald": Mox("#3f8f4f", "#a4d3a4"),
  "Sol Ring": (
    <Frame bg={A.sky}>
      <g stroke={GOLD} strokeWidth={2.2}>
        <path d="M60 2 L60 8" />
        <path d="M60 36 L60 42" />
        <path d="M40 22 L34 22" />
        <path d="M80 22 L86 22" />
        <path d="M46 8 L42 4" />
        <path d="M74 8 L78 4" />
        <path d="M46 36 L42 40" />
        <path d="M74 36 L78 40" />
      </g>
      <circle cx={60} cy={22} r={11} fill="none" stroke={GOLD} strokeWidth={6} />
      <circle cx={60} cy={22} r={11} fill="none" stroke={A.dark} strokeWidth={1.4} />
    </Frame>
  ),
  "Black Lotus": (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={35} />
      <g fill={K.dark}>
        <path d="M60 34 Q52 24 60 10 Q68 24 60 34 Z" />
        <path d="M58 34 Q44 30 40 16 Q56 20 60 32 Z" />
        <path d="M62 34 Q76 30 80 16 Q64 20 60 32 Z" />
        <path d="M56 34 Q40 36 30 28 Q46 26 58 33 Z" />
        <path d="M64 34 Q80 36 90 28 Q74 26 62 33 Z" />
      </g>
      <path d="M60 30 Q55 24 60 16 Q65 24 60 30 Z" fill={K.mid} />
      <circle cx={60} cy={32} r={2.6} fill={GOLD} />
    </Frame>
  ),
  Millstone: (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={36} />
      <circle cx={54} cy={22} r={16} fill={A.dark} />
      <circle cx={52} cy={22} r={15} fill="#a5a294" />
      <rect x={48.5} y={18.5} width={7} height={7} fill={A.dark} />
      <g stroke={A.dark} strokeWidth={1.8}>
        <path d="M52 9 L52 16" />
        <path d="M52 28 L52 35" />
        <path d="M39 22 L46 22" />
        <path d="M58 22 L65 22" />
        <path d="M43 13 L48 18" />
        <path d="M56 26 L61 31" />
        <path d="M61 13 L56 18" />
        <path d="M48 26 L43 31" />
      </g>
      <g fill={GOLD}>
        <circle cx={84} cy={36} r={1.6} />
        <circle cx={90} cy={34} r={1.6} />
        <circle cx={96} cy={37} r={1.6} />
        <circle cx={87} cy={39} r={1.6} />
      </g>
    </Frame>
  ),
  "Black Vise": (
    <Frame bg={A.sky}>
      <rect x={30} y={34} width={60} height={5} fill={A.dark} />
      <rect x={36} y={12} width={8} height={24} fill={A.mid} />
      <rect x={76} y={12} width={8} height={24} fill={A.mid} />
      <rect x={36} y={10} width={48} height={5} fill={A.dark} />
      <path d="M60 15 L60 6" stroke={A.dark} strokeWidth={3} />
      <path d="M50 6 L70 6" stroke={A.dark} strokeWidth={2.6} />
      <circle cx={60} cy={25} r={7.5} fill={K.mid} />
      <path d="M46 25 L52 25 M68 25 L74 25" stroke={A.dark} strokeWidth={3.4} />
    </Frame>
  ),
  "Chaos Orb": (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={37} />
      <circle cx={60} cy={20} r={15} fill={K.mid} />
      <path
        d="M60 20 Q62 10 72 12 M60 20 Q66 28 58 33 M60 20 Q50 22 49 12"
        stroke={FLAME}
        strokeWidth={2.6}
        fill="none"
      />
      <circle cx={60} cy={20} r={3} fill={GOLD} />
      <path d="M40 10 Q34 6 36 0 M80 10 Q86 6 84 0" stroke={A.dark} strokeWidth={1.8} fill="none" />
    </Frame>
  ),
  "Jalum Tome": (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={36} />
      <g fill={K.mid}>
        <path d="M60 12 Q44 6 30 10 L30 34 Q44 30 60 36 Z" />
        <path d="M60 12 Q76 6 90 10 L90 34 Q76 30 60 36 Z" />
      </g>
      <g fill={A.pale}>
        <path d="M60 14 Q46 9 34 12 L34 31 Q46 28 60 33 Z" />
        <path d="M60 14 Q74 9 86 12 L86 31 Q74 28 60 33 Z" />
      </g>
      <g stroke={A.mid} strokeWidth={1.2} fill="none">
        <path d="M38 16 Q48 14 56 17 M38 21 Q48 19 56 22 M38 26 Q48 24 56 27" />
        <path d="M82 16 Q72 14 64 17 M82 21 Q72 19 64 22 M82 26 Q72 24 64 27" />
      </g>
    </Frame>
  ),
  "Fellwar Stone": (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={36} />
      <polygon points="60,4 80,15 80,30 60,40 40,30 40,15" fill={A.dark} />
      <polygon points="60,9 75,17 75,28 60,35 45,28 45,17" fill={A.mid} />
      <g stroke={A.pale} strokeWidth={1.4} fill="none">
        <path d="M60 9 L60 35 M45 17 L75 28 M75 17 L45 28" />
      </g>
      <circle cx={60} cy={22} r={3} fill={GOLD} />
    </Frame>
  ),
  "Su-Chi": (
    <Frame bg={A.sky}>
      <Ground c={A.mid} y={35} />
      <g fill={A.dark}>
        <rect x={48} y={16} width={24} height={16} rx={2} />
        <rect x={52} y={4} width={16} height={10} rx={2} />
        <rect x={38} y={18} width={8} height={12} rx={2} />
        <rect x={74} y={18} width={8} height={12} rx={2} />
        <rect x={51} y={33} width={7} height={7} />
        <rect x={62} y={33} width={7} height={7} />
      </g>
      <rect x={55} y={7} width={10} height={3} fill={GOLD} />
      <circle cx={60} cy={24} r={4} fill={GOLD} />
      <circle cx={60} cy={24} r={1.6} fill={A.dark} />
    </Frame>
  ),
  Triskelion: (
    <Frame bg={A.sky}>
      <g fill={A.dark}>
        <polygon points="56,20 64,20 62,4 58,4" />
        <polygon points="57,24 62,27 46,38 43,34" />
        <polygon points="63,24 58,27 74,38 77,34" />
      </g>
      <g fill={A.mid}>
        <circle cx={60} cy={5} r={4.4} />
        <circle cx={45} cy={35} r={4.4} />
        <circle cx={75} cy={35} r={4.4} />
      </g>
      <circle cx={60} cy={22} r={7} fill={A.dark} />
      <circle cx={60} cy={22} r={3.4} fill={GOLD} />
    </Frame>
  ),
};

export function cardArt(name: string): JSX.Element | null {
  return ART[name] ?? null;
}

// Exposed for tests: the set of card names with authored art.
export const CARD_ART_NAMES: ReadonlyArray<string> = Object.keys(ART);
