use boardghost_launcher::commands::parse_list_boards_output;

#[test]
fn parses_two_line_output() {
    let stdout = "  ili9488_esp32s3_sim           ESP32-S3 with ILI9488 480x320 SPI display\n\
                  ssd1306_uno_sim               Arduino Uno with SSD1306 128x64 mono OLED\n";
    let boards = parse_list_boards_output(stdout);
    assert_eq!(boards.len(), 2);
    assert_eq!(boards[0].name, "ili9488_esp32s3_sim");
    assert_eq!(boards[0].description, "ESP32-S3 with ILI9488 480x320 SPI display");
    assert_eq!(boards[1].name, "ssd1306_uno_sim");
}

#[test]
fn skips_blank_lines() {
    let stdout = "\n  foo  bar baz\n\n";
    let boards = parse_list_boards_output(stdout);
    assert_eq!(boards.len(), 1);
    assert_eq!(boards[0].name, "foo");
    assert_eq!(boards[0].description, "bar baz");
}
