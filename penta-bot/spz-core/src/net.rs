//! The value MLP / policy head: numpy `SpzNet` forward pass, f64, ported
//! verbatim (tanh hidden, sigmoid output). Weights arrive as the flat
//! little-endian binary written by export_weights.py.

pub struct Mlp {
    pub inputs: usize,
    pub hidden: usize,
    pub w1: Vec<f64>, // hidden x inputs, row-major
    pub b1: Vec<f64>,
    pub w2: Vec<f64>, // hidden
    pub b2: f64,
}

impl Mlp {
    pub fn load(path: &str) -> Result<Self, String> {
        let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
        if bytes.len() < 16 {
            return Err("weight file too short".into());
        }
        let read_u64 = |off: usize| {
            u64::from_le_bytes(bytes[off..off + 8].try_into().unwrap()) as usize
        };
        let hidden = read_u64(0);
        let inputs = read_u64(8);
        let need = 16 + 8 * (hidden * inputs + hidden + hidden + 1);
        if bytes.len() != need {
            return Err(format!("weight file size {} != expected {}",
                               bytes.len(), need));
        }
        let mut off = 16;
        let mut take = |n: usize| {
            let out: Vec<f64> = bytes[off..off + 8 * n]
                .chunks_exact(8)
                .map(|c| f64::from_le_bytes(c.try_into().unwrap()))
                .collect();
            off += 8 * n;
            out
        };
        let w1 = take(hidden * inputs);
        let b1 = take(hidden);
        let w2 = take(hidden);
        let b2 = take(1)[0];
        Ok(Mlp { inputs, hidden, w1, b1, w2, b2 })
    }

    pub fn value(&self, x: &[f32]) -> f64 {
        debug_assert_eq!(x.len(), self.inputs);
        let mut out = self.b2;
        for h in 0..self.hidden {
            let row = &self.w1[h * self.inputs..(h + 1) * self.inputs];
            let mut pre = self.b1[h];
            for (w, xi) in row.iter().zip(x) {
                pre += w * f64::from(*xi);
            }
            out += self.w2[h] * pre.tanh();
        }
        1.0 / (1.0 + (-out).exp())
    }
}
