# High Level Design Document - Advanced Keybinds

## Requirements

1. Simplify common user interactions, specifically around moving the curosr around the text deleting lines/words easily.
2. Advanced features such as undo/redo

### Skipping forward a word

Allows the user to move the cursor to the next word by pressing `ctrl-e`

### Skipping backwards a word

Allows the user to move the cursor to the previous word by pressing `ctrl-q`

### Home/End Key functionality

Allows the user to skip to front/end of a line using the home/end keys

### Highlight Mode

Highlight a word with `ctrl-p` to enter hightlight mode. In this mode, moving the cursor around would select characters we have
passed over.

### Copy/Paste

When a word is highlighted, `ctrl-c` will save to the clipboard. `ctrl-p` pastes the saved text into the text file.

