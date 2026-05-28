export const primaryNavigation = [
  { label: "Changelog", href: "/changelog" },
  { label: "Research",  href: "/research"   },
  { label: "Missions",  href: "/missions"   },
  { label: "About",     href: "/about"      },
  { label: "Contact",   href: "/contact"    },
];

export const headerNavigation = [
  {
    label: "Platform",
    href: "/dolphin-explorer",
    sections: [
      {
        label: "Dolphin Explorer",
        href: "/dolphin-explorer",
        description: "Desktop survey import, cache, map, waterfall, and metadata workflow.",
      },
      {
        label: "Changelog",
        href: "/changelog",
        description: "What has shipped across parser, viewer, storage, and validation work.",
      },
      {
        label: "Download for Windows",
        href: "/downloads/DolphinExplorer.exe",
        description: "Get the current Dolphin Explorer desktop build.",
        download: true,
      },
    ],
  },
  {
    label: "Intelligence",
    href: "/research",
    sections: [
      {
        label: "Research",
        href: "/research",
        description: "Applied subsea interpretation, review systems, and survey workflow notes.",
      },
      {
        label: "Missions",
        href: "/missions",
        description: "Operational stories from dense marine data and analyst review loops.",
      },
      {
        label: "Field Notes",
        href: "/research",
        description: "Practical thinking around import, mapping, metadata, and decision support.",
      },
    ],
  },
  {
    label: "Company",
    href: "/about",
    sections: [
      {
        label: "About Mensor",
        href: "/about",
        description: "The lab behind the ocean survey software stack.",
      },
      {
        label: "Contact",
        href: "/contact",
        description: "Talk to us about subsea intelligence, sonar tooling, or field workflows.",
      },
    ],
  },
];
