import React from "react";
import CardVisual from "./CardVisual.jsx";
import { DetailGrid, PageBanner } from "./SiteSections.jsx";

export default function AboutPage({ labPrinciples, labStats }) {
  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="About"
        title="Mensor builds software for ocean survey work."
        summary="We build desktop tools, signal processing workflows, and precise review systems for sonar data, seafloor mapping, and offshore intelligence."
        asideArt={{ pattern: "nodes", scheme: "ink" }}
        asideCaption="Lab profile"
        actions={[
          { label: "View research", href: "/research" },
          { label: "Contact Mensor", href: "/contact", solid: true },
        ]}
      />

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Profile</p>
          <h2>Lab indicators</h2>
        </div>
        <div className="stats-grid">
          {labStats.map((stat) => (
            <article key={stat.label} className="stat-card">
              <CardVisual art={stat.art} className="stat-card__media" />
              <p className="detail-card__kicker">{stat.kicker}</p>
              <strong>{stat.value}</strong>
              <p>{stat.label}</p>
              <a className="link-arrow card-link" href="/research">
                Research
              </a>
            </article>
          ))}
        </div>
      </section>

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Principles</p>
          <h2>How the lab thinks</h2>
        </div>
        <DetailGrid
          items={labPrinciples}
          ctaHref="/research"
          ctaLabel="Read research"
        />
      </section>
    </div>
  );
}
