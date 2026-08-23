"""Aggregation of meta-variant and template-specialisation type sources.

This module handles two collapsing passes before scoring:

1. **Meta variants** — types carrying a designating prefix or suffix
   (e.g. ``VehicleDefinition`` with suffix ``Definition``) are folded
   into their base type (``Vehicle``).  The meta-variant types are then
   removed from the resolution set.

2. **Template specialisations** — instantiations such as
   ``WeakRef<Actor>`` are grouped under their stripped base type
   (``WeakRef``).  Source files from all specialisations are merged
   together and the instantiation types are removed from the set.
"""

import logging

from dumppdb_tools.resolution.resolver import get_meta_variants, strip_template_args


class TypeAggregator:
    """Merge meta-variant and template-specialisation type sources.

    Usage::

        agg = TypeAggregator(meta_prefixes, meta_suffixes)
        agg.process(type_sources)
        # agg.aggregated_sources  → {type_name: set[str]}
        # agg.meta_type_links     → {base: [meta_variant, ...]}
        # agg.template_type_links → {base: {specialisation, ...}}
    """

    def __init__(
        self,
        meta_prefixes: list[str] | None = None,
        meta_suffixes: list[str] | None = None,
    ) -> None:
        self.meta_prefixes = meta_prefixes or []
        self.meta_suffixes = meta_suffixes or []

        # Populated after :meth:`process`.
        self.aggregated_sources: dict[str, set[str]] = {}
        self.meta_variant_types: set[str] = set()
        self.template_variant_types: set[str] = set()
        self.meta_type_links: dict[str, list[str]] = {}
        self.template_type_links: dict[str, set[str]] = {}

    def process(self, type_sources: dict[str, set[str]]) -> None:
        """Run both aggregation passes and populate all result attributes."""
        self._aggregate_meta(type_sources)
        self._aggregate_templates(type_sources)
        self._build_aggregated(type_sources)

    # ------------------------------------------------------------------

    def _aggregate_meta(self, type_sources: dict[str, set[str]]) -> None:
        if not self.meta_prefixes and not self.meta_suffixes:
            return

        meta_sources: dict[str, set[str]] = {}

        for type_name, sources in type_sources.items():
            for base in get_meta_variants(
                type_name, self.meta_prefixes, self.meta_suffixes
            ):
                if base not in type_sources:
                    continue
                meta_sources.setdefault(base, set()).update(sources)
                self.meta_variant_types.add(type_name)
                self.meta_type_links.setdefault(base, []).append(type_name)

        self._meta_sources = meta_sources

        if meta_sources:
            logging.info("Meta type sources merged:")
            for base, srcs in sorted(meta_sources.items()):
                logging.info("  %s += %d source(s)", base, len(srcs))

        if self.meta_variant_types:
            logging.info(
                "Meta-variant types removed from resolution: %d",
                len(self.meta_variant_types),
            )

        if self.meta_type_links:
            logging.info("Meta type links (base -> meta variants):")
            for base, variants in sorted(self.meta_type_links.items()):
                logging.info("  %s -> %s", base, ", ".join(sorted(set(variants))))

    def _aggregate_templates(self, type_sources: dict[str, set[str]]) -> None:
        template_sources: dict[str, set[str]] = {}

        for type_name, sources in type_sources.items():
            base = strip_template_args(type_name)
            if base == type_name or not base:
                continue
            self.template_variant_types.add(type_name)
            template_sources.setdefault(base, set()).update(sources)
            self.template_type_links.setdefault(base, set()).add(type_name)

        self._template_sources = template_sources

        if template_sources:
            logging.info("Template specialisation sources merged:")
            for base, srcs in sorted(template_sources.items()):
                inst_count = len(self.template_type_links.get(base, set()))
                logging.info(
                    "  %s += %d source(s) from %d specialisation(s)",
                    base,
                    len(srcs),
                    inst_count,
                )

        if self.template_variant_types:
            logging.info(
                "Template instantiation types removed from resolution: %d",
                len(self.template_variant_types),
            )

        if self.template_type_links:
            logging.info("Template type links (base -> instantiations):")
            for base, insts in sorted(self.template_type_links.items()):
                logging.info("  %s -> %s", base, ", ".join(sorted(insts)))

    def _build_aggregated(self, type_sources: dict[str, set[str]]) -> None:
        """Merge per-type source sets into the final aggregated map."""
        meta_sources = getattr(self, "_meta_sources", {})
        template_sources = getattr(self, "_template_sources", {})

        for type_name, sources in type_sources.items():
            if type_name in self.meta_variant_types:
                continue
            if type_name in self.template_variant_types:
                continue

            merged = set(sources)
            merged.update(meta_sources.get(type_name, set()))
            merged.update(template_sources.get(type_name, set()))

            # Fold in the base type's sources when this type is itself a
            # meta variant (e.g. CVehicle inherits Vehicle's sources).
            for base in get_meta_variants(
                type_name, self.meta_prefixes, self.meta_suffixes
            ):
                if base in type_sources:
                    merged.update(type_sources[base])

            self.aggregated_sources[type_name] = merged

        # Template base types that exist only via specialisations.
        for base_name, tsrcs in template_sources.items():
            if base_name in self.aggregated_sources:
                continue
            if base_name in self.meta_variant_types:
                continue

            merged = set(tsrcs)
            merged.update(type_sources.get(base_name, set()))
            merged.update(meta_sources.get(base_name, set()))

            for base in get_meta_variants(
                base_name, self.meta_prefixes, self.meta_suffixes
            ):
                if base in type_sources:
                    merged.update(type_sources[base])

            self.aggregated_sources[base_name] = merged
