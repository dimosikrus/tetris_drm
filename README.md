# tetris_drm
Tetris DRM (Direct Rendering Manager)

```
Created with [ChatGPT](https://chatgpt.com) & [Gemini 2.5 Pro](https://aistudio.google.com)
Tested on ArchLinux 64bit & Ubuntu VPS
(my card is /dev/dri/card1) (default is card0 (edit code manually))
Working only without DE (Desktop Enviroment) (Only Linux Terminal Mode)
```

```bash
# Archlinux
sudo pacman -S libdrm
# or
yay -S libdrm

# Ubuntu
sudo apt install libdrm-dev -y
```

## Build & Run
```bash
chmod +x ./build.sh
./build.sh
./tetris_drm
```

# Controls
```
LEFT/RIGHT - Move Horizontally
UP - Rotate piece
DOWN - Fast drop
Q - Rage quit (exit)
```

<img width="1084" height="814" alt="image" src="https://github.com/user-attachments/assets/529e92d6-12c1-4984-81d4-30a74b389203" />
