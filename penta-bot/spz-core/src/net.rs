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

    /// Values for `m` rows at once (`m * inputs`, row-major).
    ///
    /// Same reason as Actor::score_batch: evaluating one row at a time
    /// re-streams the whole w1 matrix per row, and at 256x1081 f64 that is
    /// 2.2 MB -- past L2, so every row pays a trip to memory. Hoisting the
    /// hidden loop outside the row loop reads each weight row once and
    /// reuses it across all m. Accumulation order per row is unchanged, so
    /// results are bit-identical to calling value() in a loop.
    pub fn value_batch(&self, rows: &[f32], m: usize) -> Vec<f64> {
        debug_assert_eq!(rows.len(), m * self.inputs);
        let mut out = vec![self.b2; m];
        let mut pre = vec![0.0f64; m];
        for h in 0..self.hidden {
            let w = &self.w1[h * self.inputs..(h + 1) * self.inputs];
            let bias = self.b1[h];
            for (j, p) in pre.iter_mut().enumerate() {
                let x = &rows[j * self.inputs..(j + 1) * self.inputs];
                let mut acc = bias;
                for (wi, xi) in w.iter().zip(x) {
                    acc += wi * f64::from(*xi);
                }
                *p = acc;
            }
            let w2h = self.w2[h];
            for (o, p) in out.iter_mut().zip(&pre) {
                *o += w2h * p.tanh();
            }
        }
        out.iter_mut().for_each(|o| *o = 1.0 / (1.0 + (-*o).exp()));
        out
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
