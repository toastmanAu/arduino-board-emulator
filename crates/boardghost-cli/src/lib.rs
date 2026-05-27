pub mod board;
pub mod cli;
pub mod discover;
pub mod doctor;
pub mod error;
pub mod preprocess;

pub use board::{BoardProfile, DisplayConfig, TouchConfig};
pub use cli::{BuildProfile, Cli, Command};
pub use error::BoardGhostError;
