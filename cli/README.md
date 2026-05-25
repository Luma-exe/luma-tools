# luma-tools-cli

Command-line client for [Luma Tools](https://tools.lumaplayground.com). Compress, convert, and transform files from your terminal.

## Install

```bash
npx luma-tools-cli help
```

Or globally:
```bash
npm install -g luma-tools-cli
luma help
```

## Auth

Generate an API key at https://tools.lumaplayground.com/account → **API keys** (requires Pro).

```bash
luma login         # paste the lt_xxx key, saved to ~/.luma/key
luma whoami        # confirm
```

Or set `LUMA_API_KEY` in your environment.

## Use

```bash
luma image-compress photo.jpg --out small.jpg
luma video-to-gif clip.mp4 --out clip.gif
luma pdf-merge a.pdf b.pdf c.pdf --out merged.pdf
luma list          # see all supported tools
```

Pass any extra parameters as `--name=value`:
```bash
luma image-resize photo.jpg --w=800 --h=600
```

## License

MIT.
