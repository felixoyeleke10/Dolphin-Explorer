import React from "react";

const footerGroups = [
  {
    title: "Platform",
    links: [
      { label: "Dolphin Explorer", href: "/dolphin-explorer" },
      { label: "Changelog", href: "/changelog" },
      { label: "Download for Windows", href: "/downloads/DolphinExplorer.exe", download: true },
    ],
  },
  {
    title: "Lab",
    links: [
      { label: "Research", href: "/research" },
      { label: "Missions", href: "/missions" },
      { label: "About", href: "/about" },
    ],
  },
  {
    title: "Connect",
    links: [
      { label: "Contact", href: "/contact" },
      { label: "hello@mensor.ai", href: "mailto:hello@mensor.ai" },
    ],
  },
];

export default function Footer() {
  const year = new Date().getFullYear();

  return (
    <footer className="site-footer">
      <div className="site-footer__top">
        <div className="site-footer__intro">
          <p className="site-footer__brand">Mensor</p>
          <p className="site-footer__summary">
            Ocean survey software, sonar processing workflows, and
            intelligent mapping tools for field teams.
          </p>
        </div>

        <div className="site-footer__groups" aria-label="Footer navigation">
          {footerGroups.map((group) => (
            <div key={group.title} className="site-footer__group">
              <p className="site-footer__label">{group.title}</p>
              <div className="site-footer__links">
                {group.links.map((link) => (
                  <a key={link.label} href={link.href} download={link.download || undefined}>
                    {link.label}
                  </a>
                ))}
              </div>
            </div>
          ))}
        </div>
      </div>

      <div className="site-footer__bottom">
        <p>Copyright {year} Mensor. All rights reserved.</p>
        <div className="site-footer__meta">
          <a href="/">Home</a>
          <a href="/contact">Contact</a>
        </div>
      </div>
    </footer>
  );
}
