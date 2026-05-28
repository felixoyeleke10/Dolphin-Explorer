import React from "react";
import CardVisual from "./CardVisual.jsx";
import { PageBanner } from "./SiteSections.jsx";

const contactChannels = [
  {
    kicker: "Research partnerships",
    title: "Applied subsea AI",
    body:
      "Talk with Mensor about marine data workflows, interpretation systems, and research collaborations shaped around operational use.",
    href: "mailto:hello@astramarine.ai",
    label: "hello@astramarine.ai",
    art: { pattern: "nodes", scheme: "seafoam" },
  },
  {
    kicker: "Software",
    title: "Dolphin Explorer deployments",
    body:
      "If you want to evaluate the desktop software or discuss fit for a mission environment, start here.",
    href: "/downloads/DolphinExplorer.exe",
    label: "Download Windows build",
    download: true,
    art: { pattern: "panel", scheme: "sunset", label: "Download" },
  },
];

export default function ContactPage() {
  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="Contact"
        title="Talk to the lab."
        summary="For research partnerships, software deployments, and marine data discussions."
        asideEyebrow="Channel"
        asideTitle="Small lab, direct conversations"
        asideBody="The shortest path between a mission problem and the right technical conversation."
        asideArt={{ pattern: "nodes", scheme: "seafoam" }}
        asideCaption="Direct contact"
        actions={[
          { label: "Email Mensor", href: "mailto:hello@astramarine.ai", solid: true },
          { label: "View software", href: "/dolphin-explorer" },
        ]}
      />

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Channels</p>
          <h2>Primary ways to reach us</h2>
        </div>

        <div className="contact-grid">
          {contactChannels.map((channel) => (
            <article key={channel.title} className="channel-card">
              <CardVisual art={channel.art} className="channel-card__media" />
              <p className="detail-card__kicker">{channel.kicker}</p>
              <h3>{channel.title}</h3>
              <p>{channel.body}</p>
              <a
                className="link-arrow card-link"
                href={channel.href}
                download={channel.download || undefined}
              >
                {channel.label}
              </a>
            </article>
          ))}
        </div>
      </section>
    </div>
  );
}
