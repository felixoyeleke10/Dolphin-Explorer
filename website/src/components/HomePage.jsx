import React from "react";

const proofItems = [
  "XTF",
  "JSF",
  "SEG-Y",
  "DLPD",
  "2D/3D review",
  "Node graph",
];

const productTabs = [
  {
    label: "Import",
    title: "Open messy survey data without losing the mission thread.",
    body: "Register sources, reuse valid parsed caches, and avoid repeat decoding when projects reopen.",
  },
  {
    label: "Interpret",
    title: "Move from raw sonar to operator-ready context.",
    body: "Review waterfall, map swaths, metadata, nav corrections, and sub-bottom traces in one shell.",
  },
  {
    label: "Decide",
    title: "Turn vessel-side review into durable intelligence.",
    body: "Keep source identity, project state, processing notes, and export paths connected.",
  },
];

const intelligenceCards = [
  {
    eyebrow: "Desktop platform",
    title: "Dolphin Explorer",
    body: "A field-ready Windows workspace for importing, mapping, reviewing, and interpreting marine survey files.",
    href: "/dolphin-explorer",
  },
  {
    eyebrow: "Research layer",
    title: "Subsea intelligence",
    body: "Signal processing, GPU inference, anomaly triage, and mission workflows shaped around offshore conditions.",
    href: "/research",
  },
  {
    eyebrow: "Operational record",
    title: "Build log",
    body: "A transparent release trail covering project storage, parser coverage, map views, metadata, and validation.",
    href: "/changelog",
  },
];

const workflowItems = [
  "Import-once project registration",
  "Durable .dlp and .dlpd storage",
  "Sidescan waterfall and seabed tracking",
  "2D swath mapping and OpenGL 3D view",
  "SSS and SBP metadata review",
  "CTest-backed parser and viewport checks",
];

const faqs = [
  {
    q: "What is Mensor building?",
    a: "A subsea intelligence platform starting with Dolphin Explorer, a native desktop app for ocean survey review.",
  },
  {
    q: "Who is it for?",
    a: "Survey teams, analysts, research labs, and offshore operators who need dense marine data to become usable context.",
  },
  {
    q: "Why desktop first?",
    a: "Marine files are large, field bandwidth is uneven, and operators need fast local review close to the data.",
  },
];

export default function HomePage() {
  return (
    <>
      <section className="home-hero">
        <div className="home-hero__inner">
          <p className="home-hero__eyebrow">Mensor Subsea Intelligence</p>
          <h1>AI-native software for ocean survey.</h1>
          <p className="home-hero__lede">
            Dolphin Explorer turns sonar, navigation, metadata, and mission context
            into a single field-ready workspace for subsea teams.
          </p>
          <div className="home-hero__actions">
            <a className="home-button home-button--dark" href="/dolphin-explorer">
              Try Dolphin Explorer
            </a>
            <a className="home-button home-button--light" href="/research">
              Explore intelligence
            </a>
          </div>
        </div>

        <div className="home-product" aria-label="Dolphin Explorer interface preview">
          <div className="home-product__chrome">
            <span />
            <span />
            <span />
          </div>
          <div className="home-product__layout">
            <aside className="home-product__rail">
              <strong>Mission</strong>
              <span>Survey-024</span>
              <span>North grid</span>
              <span>Line 18</span>
            </aside>
            <div className="home-product__map">
              <div className="home-product__track home-product__track--one" />
              <div className="home-product__track home-product__track--two" />
              <div className="home-product__target" />
            </div>
            <aside className="home-product__panel">
              <span>Confidence</span>
              <strong>92%</strong>
              <div className="home-product__meter">
                <i />
              </div>
              <p>Cache valid. Nav aligned. Review ready.</p>
            </aside>
          </div>
        </div>

        <div className="home-proof" aria-label="Supported workflow signals">
          {proofItems.map((item) => (
            <span key={item}>{item}</span>
          ))}
        </div>
      </section>

      <section className="home-band home-band--product">
        <div className="home-section-heading">
          <p>Platform</p>
          <h2>One workspace from raw files to subsea decisions.</h2>
        </div>
        <div className="home-tabs">
          {productTabs.map((tab) => (
            <article key={tab.label} className="home-tabs__item">
              <span>{tab.label}</span>
              <h3>{tab.title}</h3>
              <p>{tab.body}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="home-band">
        <div className="home-section-heading home-section-heading--split">
          <div>
            <p>Intelligence stack</p>
            <h2>The OpenAI-shaped layer for subsea operations.</h2>
          </div>
          <a className="home-text-link" href="/about">About Mensor</a>
        </div>
        <div className="home-intelligence-grid">
          {intelligenceCards.map((card) => (
            <a key={card.title} className="home-intelligence-card" href={card.href}>
              <span>{card.eyebrow}</span>
              <h3>{card.title}</h3>
              <p>{card.body}</p>
            </a>
          ))}
        </div>
      </section>

      <section className="home-workflow">
        <div className="home-workflow__content">
          <p>Built progress</p>
          <h2>Everything achieved in the app is now visible on the site.</h2>
          <a className="home-button home-button--light" href="/changelog">
            View changelog
          </a>
        </div>
        <div className="home-workflow__list">
          {workflowItems.map((item) => (
            <span key={item}>{item}</span>
          ))}
        </div>
      </section>

      <section className="home-band">
        <div className="home-section-heading">
          <p>Questions</p>
          <h2>Clear enough for buyers, serious enough for builders.</h2>
        </div>
        <div className="home-faq">
          {faqs.map((item) => (
            <article key={item.q}>
              <h3>{item.q}</h3>
              <p>{item.a}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="home-final">
        <p>Ready for field data.</p>
        <h2>Bring subsea intelligence into one operating system.</h2>
        <a className="home-button home-button--light" href="/contact">
          Contact Mensor
        </a>
      </section>
    </>
  );
}
