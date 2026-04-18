# ReZygisk's Treat Wheel

World's most advanced and simplest general purpose root module.

## Features

- C99
- Traceless
- Low complexity

## Requirements

- Magisk Official, KernelSU Official (or API compliant), or APatch
- ReZygisk 498 or higher
- Android 7.1 or higher

## Support

Any question or issue related to Treat Wheel can be made in our:

- [Telegram chat](https://t.me/performancorg)
- [Signal group](https://signal.performanc.org)

> [!WARNING]
> Absolutely NO support will be given if requirements are NOT met.

## Usage

The only feature of Treat Wheel that requires setup is RVU (ReVanced Umount). ReVanced modules MUST include a `tw_config` file in their module folder with the following content:

```properties
module_type=revanced
allow_umount=true
```

Which will allow Treat Wheel to enumerate the amount of ReVanced modules -- hence amount of mounts it should find -- and umount them.

## Contribution

It is mandatory to follow the PerformanC's [contribution guidelines](https://github.com/PerformanC/contributing) to contribute to Treat Wheel. Following its Security Policy, Code of Conduct and syntax standard.

## License

Treat Wheel is licensed under [AGPLv3 License](LICENSE). You can read more about it on [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).

* This project is considered as: [leading standard](https://github.com/PerformanC/contributing?tab=readme-ov-file#project-information).
