# -*- coding: utf-8 -*-
#
# Copyright 2019, Oath Inc.
# SPDX-License-Identifier: Apache-2.0

"""
    Transaction Box Sphinx Directives
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx Docs directives for Transaction Box plugin.
"""

from docutils import nodes
from docutils.parsers import rst
from docutils.parsers.rst import directives
from sphinx.domains import Domain, ObjType, std
from sphinx.roles import XRefRole
from sphinx.locale import l_, _
import sphinx

import re


class TxbDirective(std.Target):
    """
    Directive description.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True
    has_content = True

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0];
        txb_id = nodes.make_id(txb_name);

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'directive'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'directive'
        self.add_name(title)
        title.set_class('directive-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)

        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['directive'][txb_name] = env.docname

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        if sphinx.version_info >= (1, 4):
            indexnode['entries'].append(
                ('single', _('%s') % txb_name, txb_id, '', '')
            )
        else:
            indexnode['entries'].append(
                ('single', _('%s') % txb_name, txb_id, '')
            )

        return [indexnode, node, nodes.field_list(), nn]


class TxbExtractor(std.Target):
    """
    Extractor description.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True
    has_content = True

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0];
        txb_id = nodes.make_id(txb_name);

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'extractor'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'extractor'
        self.add_name(title)
        title.set_class('directive-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)

        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['extractor'][txb_name] = env.docname

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, nodes.field_list(), nn]

class TxbDirectiveRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target

class TxbExtractorRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target

class TxbComparison(std.Target):
    """
    Comparison description.

    Descriptive text should follow, indented.

    Then the bulk description (if any) undented. This should be considered equivalent to the Doxygen
    short and long description.
    """

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True
    has_content = True

    # External entry point
    def run(self):
        env = self.state.document.settings.env
        txb_name = self.arguments[0];
        txb_id = nodes.make_id(txb_name);

        # First, make a generic desc() node to be the parent.
        node = sphinx.addnodes.desc()
        node.document = self.state.document
        node['objtype'] = 'comparison'

        # Next, make a signature node. This creates a permalink and a highlighted background when the link is selected.
        title = sphinx.addnodes.desc_signature(txb_name, '')
        title['ids'].append(txb_id)
        title['names'].append(txb_name)
        title['first'] = False
        title['objtype'] = 'comparison'
        self.add_name(title)
        title.set_class('comparison-title')

        # Finally, add a desc_name() node to display the name of the
        # configuration variable.
        title += sphinx.addnodes.desc_name(txb_name, txb_name)

        node.append(title)

        if ('class' in self.options):
            title.set_class(self.options.get('class'))

        # This has to be a distinct node before the title. if nested then the browser will scroll forward to just past the title.
        anchor = nodes.target('', '', names=[txb_name])
        # Second (optional) arg is 'msgNode' - no idea what I should pass for that
        # or if it even matters, although I now think it should not be used.
        self.state.document.note_explicit_target(title)
        env.domaindata['txb']['comparison'][txb_name] = env.docname

        # Get any contained content
        nn = nodes.compound()
        self.state.nested_parse(self.content, self.content_offset, nn)

        # Create an index node so that Sphinx adds this directive to the index.
        indexnode = sphinx.addnodes.index(entries=[])
        indexnode['entries'].append(('single', _('%s') % txb_name, txb_id, '', ''))

        return [indexnode, node, nodes.field_list(), nn]

class TxbComparisonRef(XRefRole):
    def process_link(self, env, ref_node, explicit_title_p, title, target):
        return title, target

class TxnBoxDomain(Domain):
    """
    Transaction Box Documentation.
    """

    name = 'txb'
    label = 'Transaction Box'
    data_version = 2

    object_types = {
        'directive': ObjType(_('Directive'), 'directive'),
        'extractor': ObjType(_('Extractor'), 'extractor')
    }

    directives = {
        'directive': TxbDirective,
        'extractor': TxbExtractor,
        'comparison': TxbComparison
    }


    roles = {
        'directive': TxbDirectiveRef(),
        'extractor': TxbExtractorRef(),
        'ex': TxbExtractorRef(),
        'comparison': TxbComparisonRef(),
        'cmp': TxbComparisonRef(),
    }

    initial_data = {
        'directive': {},  # full name -> docname
        'extractor': {},
        'comparison': {},
    }

    dangling_warnings = {
        'directive': "No definition found for directive '%(target)s'",
        'extractor': "No definition found for extractor '%(target)s'",
        'comparison': "No definition found for comparison '%(target)s'"
    }

    def clear_doc(self, docname):
        tmp_list = self.data['directive'];
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]
        tmp_list = self.data['extractor'];
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]
        tmp_list = self.data['comparison'];
        for var, doc in list(tmp_list.items()):
            if doc == docname:
                del tmp_list[var]

    def find_doc(self, key, obj_type):
        zret = None

        if obj_type == 'directive':
            obj_list = self.data['directive']
        elif obj_type == 'extractor':
            obj_list = self.data['extractor']
        elif obj_type == 'comparison':
            obj_list = self.data['comparison']
        else:
            obj_list = None

        if obj_list and key in obj_list:
            zret = obj_list[key]

        return zret

    def resolve_xref(self, env, src_doc, builder, obj_type, target, node, cont_node):
        dst_doc = self.find_doc(target, obj_type)
        if (dst_doc):
            return sphinx.util.nodes.make_refnode(builder, src_doc, dst_doc, nodes.make_id(target), cont_node, target)

    def get_objects(self):
        for var, doc in self.data['directive'].items():
            yield var, var, 'directive', doc, var, 1
        for var, doc in self.data['extractor'].items():
            yield var, var, 'extractor', doc, var, 1
        for var, doc in self.data['comparison'].items():
            yield var, var, 'comparison', doc, var, 1


def setup(app):
    rst.roles.register_generic_role('arg', nodes.emphasis)
    rst.roles.register_generic_role('const', nodes.literal)

    app.add_domain(TxnBoxDomain)
