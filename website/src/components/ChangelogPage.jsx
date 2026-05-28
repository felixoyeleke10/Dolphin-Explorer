import React from "react";
import CardVisual from "./CardVisual.jsx";
import { changelogEntries } from "../data/siteData.js";

export default function ChangelogPage() {
  return (
    <div className="page-stack">
      {/* Banner */}
      <section className="page-banner">
        <div className="page-banner__content">
          <p className="section-kicker">Changelog</p>
          <h1>What's been built.</h1>
          <p className="page-banner__summary">
            A versioned record of every feature, module, and architectural
            decision shipped in Dolphin Explorer and the Mensor platform.
          </p>
        </div>
      </section>

      {/* Entries */}
      <div className="changelog">
        {changelogEntries.map((entry) => (
          <article key={entry.id} className="changelog-entry">
            {/* Left — date + version + tags */}
            <div className="changelog-entry__meta">
              <span className="changelog-entry__date">{entry.date}</span>
              <span className="changelog-entry__version">{entry.version}</span>
              <div className="changelog-entry__tags">
                {entry.tags.map((tag) => (
                  <span key={tag} className="changelog-tag">{tag}</span>
                ))}
              </div>
            </div>

            {/* Right — content */}
            <div className="changelog-entry__content">
              <h2>{entry.title}</h2>
              <p className="changelog-entry__body">{entry.body}</p>

              {entry.items?.length > 0 && (
                <ul className="changelog-entry__items">
                  {entry.items.map((item, i) => (
                    <li key={i}>{item}</li>
                  ))}
                </ul>
              )}

              {entry.art && (
                <div className="changelog-entry__art">
                  <CardVisual art={entry.art} />
                </div>
              )}

              {entry.link && (
                <a className="changelog-entry__link" href={entry.link}>
                  View details
                </a>
              )}
            </div>
          </article>
        ))}
      </div>
    </div>
  );
}
