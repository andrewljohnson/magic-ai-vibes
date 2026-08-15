use std::error::Error;
use std::fmt;
use std::str::FromStr;

use super::HybridPair;

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
pub struct ManaCost {
    pub generic: u16,
    pub white: u16,
    pub blue: u16,
    pub black: u16,
    pub red: u16,
    pub green: u16,
    /// How many hybrid symbols of each colour pair this cost carries, indexed
    /// by [`HybridPair::index`].
    pub hybrid: [u16; HybridPair::COUNT],
    pub variable_x: bool,
    pub x_multiplier: u16,
}

/// Why a symbolic mana-cost string could not be represented by [`ManaCost`].
///
/// Penta accepts the canonical braced notation used by Oracle, such as
/// `{2}{G}{G}` or `{X}{R}`. Symbols outside the engine's current mana model
/// are rejected instead of being approximated.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ManaCostParseError {
    pub offset: usize,
    pub kind: ManaCostParseErrorKind,
}

impl ManaCostParseError {
    const fn new(offset: usize, kind: ManaCostParseErrorKind) -> Self {
        Self { offset, kind }
    }
}
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum ManaCostParseErrorKind {
    Empty,
    ExpectedOpeningBrace,
    UnterminatedSymbol,
    EmptySymbol,
    InvalidSymbol,
    DuplicateGenericSymbol,
    Overflow,
}

impl fmt::Display for ManaCostParseError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let problem = match self.kind {
            ManaCostParseErrorKind::Empty => "a mana cost cannot be empty",
            ManaCostParseErrorKind::ExpectedOpeningBrace => {
                "each mana symbol must start with an opening brace"
            }
            ManaCostParseErrorKind::UnterminatedSymbol => {
                "a mana symbol is missing its closing brace"
            }
            ManaCostParseErrorKind::EmptySymbol => "a mana symbol cannot be empty",
            ManaCostParseErrorKind::InvalidSymbol => {
                "the mana symbol is invalid or unsupported by the current engine"
            }
            ManaCostParseErrorKind::DuplicateGenericSymbol => {
                "a mana cost may contain only one numeric generic symbol"
            }
            ManaCostParseErrorKind::Overflow => "the mana cost exceeds the supported numeric range",
        };
        write!(formatter, "{problem} at byte {}", self.offset)
    }
}

impl Error for ManaCostParseError {}

impl ManaCost {
    /// Parses canonical braced mana symbols without allocating.
    ///
    /// This is `const` so [`crate::mana_cost!`] can validate literals during
    /// compilation. Runtime callers will usually prefer `str::parse`, which
    /// uses the same parser through [`FromStr`]. An empty string is invalid:
    /// a card with no mana cost is represented by [`PrintedManaCost::None`],
    /// while `{0}` is a real, payable printed cost.
    ///
    /// # Errors
    ///
    /// Returns a [`ManaCostParseError`] at the first malformed or currently
    /// unsupported symbol, duplicate numeric symbol, or numeric overflow.
    #[allow(clippy::too_many_lines)]
    pub const fn parse_symbols(symbols: &str) -> Result<Self, ManaCostParseError> {
        let bytes = symbols.as_bytes();
        if bytes.is_empty() {
            return Err(ManaCostParseError::new(0, ManaCostParseErrorKind::Empty));
        }

        let mut cost = Self {
            generic: 0,
            white: 0,
            blue: 0,
            black: 0,
            red: 0,
            green: 0,
            hybrid: [0; HybridPair::COUNT],
            variable_x: false,
            x_multiplier: 0,
        };
        let mut offset = 0;
        let mut saw_generic = false;

        while offset < bytes.len() {
            if bytes[offset] != b'{' {
                return Err(ManaCostParseError::new(
                    offset,
                    ManaCostParseErrorKind::ExpectedOpeningBrace,
                ));
            }
            let symbol_start = offset + 1;
            let mut symbol_end = symbol_start;
            while symbol_end < bytes.len() && bytes[symbol_end] != b'}' {
                symbol_end += 1;
            }
            if symbol_end == bytes.len() {
                return Err(ManaCostParseError::new(
                    offset,
                    ManaCostParseErrorKind::UnterminatedSymbol,
                ));
            }
            if symbol_end == symbol_start {
                return Err(ManaCostParseError::new(
                    symbol_start,
                    ManaCostParseErrorKind::EmptySymbol,
                ));
            }

            let symbol_len = symbol_end - symbol_start;
            if symbol_len == 1 {
                let parsed = match bytes[symbol_start] {
                    b'W' => Self::checked_increment(cost.white),
                    b'U' => Self::checked_increment(cost.blue),
                    b'B' => Self::checked_increment(cost.black),
                    b'R' => Self::checked_increment(cost.red),
                    b'G' => Self::checked_increment(cost.green),
                    b'X' => Self::checked_increment(cost.x_multiplier),
                    b'0'..=b'9' => {
                        if saw_generic {
                            return Err(ManaCostParseError::new(
                                symbol_start,
                                ManaCostParseErrorKind::DuplicateGenericSymbol,
                            ));
                        }
                        saw_generic = true;
                        Ok((bytes[symbol_start] - b'0') as u16)
                    }
                    _ => Err(ManaCostParseErrorKind::InvalidSymbol),
                };
                let value = match parsed {
                    Ok(value) => value,
                    Err(kind) => return Err(ManaCostParseError::new(symbol_start, kind)),
                };
                match bytes[symbol_start] {
                    b'W' => cost.white = value,
                    b'U' => cost.blue = value,
                    b'B' => cost.black = value,
                    b'R' => cost.red = value,
                    b'G' => cost.green = value,
                    b'X' => {
                        cost.variable_x = true;
                        cost.x_multiplier = value;
                    }
                    b'0'..=b'9' => cost.generic = value,
                    _ => {}
                }
            } else if symbol_len == 3
                && bytes[symbol_start + 1] == b'/'
                && let Some(pair) =
                    HybridPair::from_letters(bytes[symbol_start], bytes[symbol_start + 2])
            {
                let index = pair.index();
                cost.hybrid[index] = match Self::checked_increment(cost.hybrid[index]) {
                    Ok(value) => value,
                    Err(kind) => return Err(ManaCostParseError::new(symbol_start, kind)),
                };
            } else {
                let first = bytes[symbol_start];
                if !first.is_ascii_digit() {
                    return Err(ManaCostParseError::new(
                        symbol_start,
                        ManaCostParseErrorKind::InvalidSymbol,
                    ));
                }
                if saw_generic {
                    return Err(ManaCostParseError::new(
                        symbol_start,
                        ManaCostParseErrorKind::DuplicateGenericSymbol,
                    ));
                }
                if first == b'0' {
                    return Err(ManaCostParseError::new(
                        symbol_start,
                        ManaCostParseErrorKind::InvalidSymbol,
                    ));
                }
                let mut value = 0_u16;
                let mut digit = symbol_start;
                while digit < symbol_end {
                    let byte = bytes[digit];
                    if !byte.is_ascii_digit() {
                        return Err(ManaCostParseError::new(
                            digit,
                            ManaCostParseErrorKind::InvalidSymbol,
                        ));
                    }
                    value = match value.checked_mul(10) {
                        Some(value) => value,
                        None => {
                            return Err(ManaCostParseError::new(
                                symbol_start,
                                ManaCostParseErrorKind::Overflow,
                            ));
                        }
                    };
                    value = match value.checked_add((byte - b'0') as u16) {
                        Some(value) => value,
                        None => {
                            return Err(ManaCostParseError::new(
                                symbol_start,
                                ManaCostParseErrorKind::Overflow,
                            ));
                        }
                    };
                    digit += 1;
                }
                cost.generic = value;
                saw_generic = true;
            }

            offset = symbol_end + 1;
        }

        Ok(cost)
    }

    const fn checked_increment(value: u16) -> Result<u16, ManaCostParseErrorKind> {
        match value.checked_add(1) {
            Some(value) => Ok(value),
            None => Err(ManaCostParseErrorKind::Overflow),
        }
    }

    /// Mana value with each `{X}` treated as zero.
    #[must_use]
    pub const fn mana_value(self) -> u16 {
        self.generic
            .saturating_add(self.white)
            .saturating_add(self.blue)
            .saturating_add(self.black)
            .saturating_add(self.red)
            .saturating_add(self.green)
            .saturating_add(self.hybrid_total())
    }

    #[must_use]
    pub const fn new(generic: u16, red: u16) -> Self {
        Self {
            generic,
            white: 0,
            blue: 0,
            black: 0,
            red,
            green: 0,
            hybrid: [0; HybridPair::COUNT],
            variable_x: false,
            x_multiplier: 0,
        }
    }

    #[must_use]
    pub const fn colored(
        generic: u16,
        white: u16,
        blue: u16,
        black: u16,
        red: u16,
        green: u16,
    ) -> Self {
        Self {
            generic,
            white,
            blue,
            black,
            red,
            green,
            hybrid: [0; HybridPair::COUNT],
            variable_x: false,
            x_multiplier: 0,
        }
    }

    #[must_use]
    pub const fn with_x(red: u16) -> Self {
        Self {
            generic: 0,
            white: 0,
            blue: 0,
            black: 0,
            red,
            green: 0,
            hybrid: [0; HybridPair::COUNT],
            variable_x: true,
            x_multiplier: 1,
        }
    }

    #[must_use]
    pub const fn colored_x(white: u16, blue: u16, black: u16, red: u16, green: u16) -> Self {
        Self {
            generic: 0,
            white,
            blue,
            black,
            red,
            green,
            hybrid: [0; HybridPair::COUNT],
            variable_x: true,
            x_multiplier: 1,
        }
    }

    #[must_use]
    pub const fn variable(
        generic: u16,
        white: u16,
        blue: u16,
        black: u16,
        red: u16,
        green: u16,
        x_multiplier: u16,
    ) -> Self {
        Self {
            generic,
            white,
            blue,
            black,
            red,
            green,
            hybrid: [0; HybridPair::COUNT],
            variable_x: true,
            x_multiplier,
        }
    }

    /// How many hybrid symbols this cost carries in total.
    #[must_use]
    pub const fn hybrid_total(&self) -> u16 {
        let mut total: u16 = 0;
        let mut index = 0;
        while index < HybridPair::COUNT {
            total = total.saturating_add(self.hybrid[index]);
            index += 1;
        }
        total
    }

    #[must_use]
    pub const fn hybrid_pair(pair: HybridPair, count: u16) -> Self {
        Self {
            generic: 0,
            white: 0,
            blue: 0,
            black: 0,
            red: 0,
            green: 0,
            hybrid: {
                let mut hybrid = [0; HybridPair::COUNT];
                hybrid[pair.index()] = count;
                hybrid
            },
            variable_x: false,
            x_multiplier: 0,
        }
    }
}

impl fmt::Display for ManaCost {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut wrote_symbol = false;
        if self.generic > 0 {
            write!(formatter, "{{{}}}", self.generic)?;
            wrote_symbol = true;
        }
        if self.variable_x {
            for _ in 0..self.x_multiplier.max(1) {
                formatter.write_str("{X}")?;
                wrote_symbol = true;
            }
        }
        for (amount, symbol) in [
            (self.white, "W"),
            (self.blue, "U"),
            (self.black, "B"),
            (self.red, "R"),
            (self.green, "G"),
        ] {
            for _ in 0..amount {
                write!(formatter, "{{{symbol}}}")?;
                wrote_symbol = true;
            }
        }
        for pair in HybridPair::ALL {
            for _ in 0..self.hybrid[pair.index()] {
                write!(formatter, "{{{}}}", pair.symbol())?;
                wrote_symbol = true;
            }
        }
        if !wrote_symbol {
            formatter.write_str("{0}")?;
        }
        Ok(())
    }
}

impl FromStr for ManaCost {
    type Err = ManaCostParseError;

    fn from_str(symbols: &str) -> Result<Self, Self::Err> {
        Self::parse_symbols(symbols)
    }
}
