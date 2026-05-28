import React, {
  startTransition,
  useDeferredValue,
  useEffect,
  useMemo,
  useState,
} from "react";
import { headerNavigation } from "./data/navigation";
import { normalizePath, routeTitles } from "./data/routes";
import {
  featuredStories,
  labPrinciples,
  labStats,
  missionNotes,
  productDetails,
  productHighlights,
  researchUpdates,
} from "./data/siteData";
import AboutPage from "./components/AboutPage.jsx";
import ChangelogPage from "./components/ChangelogPage.jsx";
import ContactPage from "./components/ContactPage.jsx";
import DolphinExplorerPage from "./components/DolphinExplorerPage.jsx";
import Footer from "./components/Footer.jsx";
import HomePage from "./components/HomePage.jsx";
import MissionsPage from "./components/MissionsPage.jsx";
import NotFoundPage from "./components/NotFoundPage.jsx";
import ResearchPage from "./components/ResearchPage.jsx";

function matchesQuery(value, query) {
  if (!query) return true;
  return value.toLowerCase().includes(query);
}

function isModifiedEvent(event) {
  return event.metaKey || event.ctrlKey || event.shiftKey || event.altKey;
}

export default function App() {
  const [searchQuery, setSearchQuery] = useState("");
  const [activeMenu, setActiveMenu] = useState(null);
  const [currentPath, setCurrentPath] = useState(() =>
    normalizePath(window.location.pathname),
  );
  const deferredQuery = useDeferredValue(searchQuery.trim().toLowerCase());

  const filteredStories = useMemo(
    () =>
      featuredStories.filter((story) =>
        [story.label, story.title, story.summary].some((item) =>
          matchesQuery(item, deferredQuery),
        ),
      ),
    [deferredQuery],
  );

  const filteredUpdates = useMemo(
    () =>
      researchUpdates.filter((update) =>
        [update.kicker, update.title, update.body].some((item) =>
          matchesQuery(item, deferredQuery),
        ),
      ),
    [deferredQuery],
  );

  useEffect(() => {
    function handlePopState() {
      setCurrentPath(normalizePath(window.location.pathname));
    }
    window.addEventListener("popstate", handlePopState);
    return () => window.removeEventListener("popstate", handlePopState);
  }, []);

  useEffect(() => {
    document.title = routeTitles[currentPath] ?? routeTitles["/404"];
    window.scrollTo({ top: 0, left: 0, behavior: "auto" });
  }, [currentPath]);

  function navigate(nextPath) {
    const normalizedPath = normalizePath(nextPath);
    if (normalizedPath === currentPath) {
      window.scrollTo({ top: 0, left: 0, behavior: "smooth" });
      return;
    }
    setActiveMenu(null);
    window.history.pushState({}, "", normalizedPath);
    startTransition(() => setCurrentPath(normalizedPath));
  }

  function handleSiteClick(event) {
    const anchor = event.target.closest("a[href]");
    if (!anchor) return;
    const href = anchor.getAttribute("href");
    if (
      !href ||
      !href.startsWith("/") ||
      anchor.hasAttribute("download") ||
      anchor.target === "_blank" ||
      event.button !== 0 ||
      isModifiedEvent(event)
    ) {
      return;
    }
    event.preventDefault();
    navigate(href);
  }

  function handleSearchSubmit(event) {
    event.preventDefault();
    if (searchQuery.trim() && currentPath !== "/research") {
      navigate("/research");
    }
  }

  function handleSearchKeyDown(event) {
    if (event.key === "Escape") {
      setSearchQuery("");
      event.currentTarget.blur();
    }
  }

  const isMenuOpen = Boolean(activeMenu);

  const sharedProps = {
    featuredStories: filteredStories,
    researchUpdates: filteredUpdates,
    searchQuery,
    searchCount: filteredStories.length + filteredUpdates.length,
  };

  let page;

  switch (currentPath) {
    case "/":
      page = <HomePage {...sharedProps} />;
      break;
    case "/changelog":
      page = <ChangelogPage />;
      break;
    case "/missions":
      page = <MissionsPage {...sharedProps} missionNotes={missionNotes} />;
      break;
    case "/research":
      page = <ResearchPage {...sharedProps} />;
      break;
    case "/dolphin-explorer":
      page = (
        <DolphinExplorerPage
          productDetails={productDetails}
          productHighlights={productHighlights}
        />
      );
      break;
    case "/about":
      page = <AboutPage labPrinciples={labPrinciples} labStats={labStats} />;
      break;
    case "/contact":
      page = <ContactPage />;
      break;
    default:
      page = <NotFoundPage />;
  }

  return (
    <div className="site-shell" onClickCapture={handleSiteClick}>
      <div className="site-header-region" onMouseLeave={() => setActiveMenu(null)}>
        <header className="site-header">
          <div className="site-header__inner">
            <a className="brand" href="/" aria-label="Mensor home">
              Mensor
            </a>

            <nav className="site-header__nav" aria-label="Primary">
              {headerNavigation.map((item) => (
                <button
                  key={item.label}
                  type="button"
                  className={activeMenu === item.label ? "nav-menu-toggle is-open" : "nav-menu-toggle"}
                  aria-expanded={isMenuOpen}
                  aria-controls="site-menu"
                  onMouseEnter={() => setActiveMenu(item.label)}
                  onFocus={() => setActiveMenu(item.label)}
                  onClick={() => setActiveMenu(item.label)}
                >
                  {item.label}
                </button>
              ))}
            </nav>

            <div className="site-header__actions">
              <form className="header-search" role="search" onSubmit={handleSearchSubmit}>
                <label className="sr-only" htmlFor="site-search">
                  Search subsea intelligence
                </label>
                <input
                  id="site-search"
                  type="search"
                  value={searchQuery}
                  onChange={(event) => setSearchQuery(event.target.value)}
                  onKeyDown={handleSearchKeyDown}
                  placeholder="Search"
                  autoComplete="off"
                />
                {searchQuery && (
                  <button
                    type="button"
                    className="header-search__clear"
                    aria-label="Clear search"
                    onClick={() => setSearchQuery("")}
                  >
                    Clear
                  </button>
                )}
              </form>
              <button
                type="button"
                className="mobile-menu-toggle"
                aria-expanded={isMenuOpen}
                aria-controls="site-menu"
                onClick={() => setActiveMenu(activeMenu ? null : headerNavigation[0].label)}
              >
                Menu
              </button>
              <a className="header-cta" href="/dolphin-explorer">
                Try Dolphin Explorer
              </a>
            </div>
          </div>
        </header>
        <div
          className={isMenuOpen ? "site-menu is-open" : "site-menu"}
          id="site-menu"
          onMouseEnter={() => setActiveMenu(activeMenu || headerNavigation[0].label)}
        >
          <div className="site-menu__inner">
            <div className="site-menu__intro">
              <p className="site-menu__eyebrow">Subsea intelligence</p>
              <h2>One workspace for ocean survey software, research, and field workflow.</h2>
              <button
                type="button"
                className="site-menu__close"
                onClick={() => setActiveMenu(null)}
              >
                Close
              </button>
            </div>

            <div className="site-menu__grid">
              {headerNavigation.map((group) => (
                <section
                  className={activeMenu === group.label ? "site-menu__group is-active" : "site-menu__group"}
                  key={group.label}
                >
                  <a className="site-menu__group-title" href={group.href}>
                    {group.label}
                  </a>
                  <div className="site-menu__links">
                    {group.sections.map((section) => (
                      <a
                        key={`${group.label}-${section.label}`}
                        className="site-menu__link"
                        href={section.href}
                        download={section.download || undefined}
                      >
                        <span>{section.label}</span>
                        <small>{section.description}</small>
                      </a>
                    ))}
                  </div>
                </section>
              ))}
            </div>
          </div>
        </div>
      </div>

      <main className="site-main">{page}</main>

      <Footer />
    </div>
  );
}
